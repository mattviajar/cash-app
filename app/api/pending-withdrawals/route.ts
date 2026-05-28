export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'
import { getOwnedKidUsernames, getParentUsernameFromRequest, isKidOwnedByParent } from '@/lib/parent-ownership'

const DENOMINATION_FIELDS = new Set([
  'bill20',
  'bill50',
  'bill100',
  'bill500',
  'bill1000',
  'coin1',
  'coin5',
  'coin10',
  'coin20',
])

const NOTE_META_PATTERN = /\s*\[denom:([a-z0-9]+);qty:(\d+)\]\s*$/i

function parseDenomination(value: unknown): string | null {
  if (typeof value !== 'string') return null
  const field = value.trim().toLowerCase()
  if (!DENOMINATION_FIELDS.has(field)) return null
  return field
}

function parseQuantity(value: unknown): number | null {
  const parsed = Number(value)
  if (!Number.isFinite(parsed)) return null
  const count = Math.round(parsed)
  return count > 0 ? count : null
}

function appendNoteMeta(note: string, denomination: string | null, quantity: number | null): string {
  if (!denomination || !quantity) return note
  return `${note} [denom:${denomination};qty:${quantity}]`
}

function splitNoteMeta(note: string): { note: string; denomination: string | null; quantity: number | null } {
  const match = note.match(NOTE_META_PATTERN)
  if (!match) {
    return { note, denomination: null, quantity: null }
  }

  const denomination = parseDenomination(match[1])
  const quantity = parseQuantity(match[2])
  const cleanNote = note.replace(NOTE_META_PATTERN, '').trim()

  return {
    note: cleanNote || 'Withdrawal request',
    denomination,
    quantity,
  }
}

export async function GET(request: Request) {
  const { searchParams } = new URL(request.url)
  const child = (searchParams.get('child') ?? '').trim().toLowerCase()
  const parentUsername = getParentUsernameFromRequest(request)

  let where: { child?: string | { in: string[] } } | undefined = child ? { child } : undefined

  if (parentUsername) {
    const ownedKids = await getOwnedKidUsernames(prisma, parentUsername)
    if (child) {
      const allowed = ownedKids.includes(child)
      where = allowed ? { child } : { child: '__none__' }
    } else {
      where = { child: ownedKids.length > 0 ? { in: ownedKids } : { in: ['__none__'] } }
    }
  }

  const rows = await prisma.pendingWithdrawal.findMany({
    where,
    orderBy: { createdAt: 'desc' },
  })

  return NextResponse.json({
    pending: rows.map((row) => ({
      ...splitNoteMeta(row.note),
      id: row.id,
      child: row.child,
      amount: row.amount,
      createdAt: row.createdAt.toISOString(),
    })),
  })
}

export async function POST(request: Request) {
  let body: unknown
  try {
    body = await request.json()
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 })
  }

  const payload = body as Record<string, unknown>
  const child = typeof payload.child === 'string' ? payload.child.trim().toLowerCase() : ''
  const parentUsername =
    typeof payload.parentUsername === 'string'
      ? payload.parentUsername.trim().toLowerCase()
      : getParentUsernameFromRequest(request)
  const note = typeof payload.note === 'string' ? payload.note.trim() : 'Withdrawal request'
  const amount = Number(payload.amount)
  const denomination = parseDenomination(payload.denomination)
  const quantity = parseQuantity(payload.quantity)

  if (!child || !Number.isFinite(amount) || amount <= 0) {
    return NextResponse.json({ error: 'Invalid child or amount' }, { status: 400 })
  }

  const kid = await prisma.account.findUnique({ where: { username: child } })
  if (!kid || kid.role !== 'kid') {
    return NextResponse.json({ error: 'Kid account not found' }, { status: 404 })
  }

  if (parentUsername) {
    const allowed = await isKidOwnedByParent(prisma, parentUsername, child)
    if (!allowed) {
      return NextResponse.json({ error: 'You can only create requests for your own kid accounts' }, { status: 403 })
    }
  }

  const item = await prisma.pendingWithdrawal.create({
    data: {
      child,
      amount: Math.round(amount * 100) / 100,
      note: appendNoteMeta(note, denomination, quantity),
    },
  })

  const itemNote = splitNoteMeta(item.note)

  return NextResponse.json({
    ok: true,
    item: {
      id: item.id,
      child: item.child,
      amount: item.amount,
      note: itemNote.note,
      denomination: itemNote.denomination,
      quantity: itemNote.quantity,
      createdAt: item.createdAt.toISOString(),
    },
  })
}

export async function DELETE(request: Request) {
  const { searchParams } = new URL(request.url)
  const id = Number(searchParams.get('id'))
  const parentUsername = getParentUsernameFromRequest(request)

  if (!Number.isFinite(id) || id <= 0) {
    return NextResponse.json({ error: 'Missing or invalid id' }, { status: 400 })
  }

  if (parentUsername) {
    const item = await prisma.pendingWithdrawal.findUnique({ where: { id: Math.floor(id) } })
    if (!item) {
      return NextResponse.json({ error: 'Pending request not found' }, { status: 404 })
    }
    const allowed = await isKidOwnedByParent(prisma, parentUsername, item.child)
    if (!allowed) {
      return NextResponse.json({ error: 'You can only modify your own kid requests' }, { status: 403 })
    }
  }

  await prisma.pendingWithdrawal.delete({ where: { id: Math.floor(id) } })
  return NextResponse.json({ ok: true })
}
