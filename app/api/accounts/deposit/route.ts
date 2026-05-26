export const dynamic = 'force-dynamic'

import type { Prisma } from '@prisma/client'
import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'
import { getParentUsernameFromRequest, isKidOwnedByParent } from '@/lib/parent-ownership'

async function findOrCreateAccountByUsername(
  tx: Prisma.TransactionClient,
  username: string,
  role: string,
  autoCreate: boolean
) {
  const normalized = username.trim().toLowerCase()
  if (!normalized) {
    return null
  }

  const existing = await tx.account.findUnique({ where: { username: normalized } })
  if (existing) {
    return existing
  }

  if (!autoCreate) {
    return null
  }

  return tx.account.create({
    data: {
      username: normalized,
      passwordHash: 'legacy-local-account',
      role: role === 'parent' ? 'parent' : 'kid',
    },
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
  const username = typeof payload.username === 'string' ? payload.username.trim().toLowerCase() : ''
  const role = typeof payload.role === 'string' ? payload.role.trim().toLowerCase() : 'kid'
  const parentUsername =
    typeof payload.parentUsername === 'string'
      ? payload.parentUsername.trim().toLowerCase()
      : getParentUsernameFromRequest(request)
  const note = typeof payload.note === 'string' ? payload.note.trim() : 'Manual deposit'
  const source = typeof payload.source === 'string' ? payload.source.trim() : 'dashboard'
  const autoCreate = payload.autoCreate !== false
  const amount = Number(payload.amount)

  if (!username) {
    return NextResponse.json({ error: 'Missing username' }, { status: 400 })
  }
  if (!Number.isFinite(amount) || amount <= 0 || amount > 10000) {
    return NextResponse.json({ error: 'Invalid amount' }, { status: 400 })
  }

  if (role === 'kid' && parentUsername) {
    const allowed = await isKidOwnedByParent(prisma, parentUsername, username)
    if (!allowed) {
      return NextResponse.json({ error: 'You can only deposit to your own kid accounts' }, { status: 403 })
    }
  }

  try {
    const normalizedAmount = Math.round(amount * 100) / 100

    const result = await prisma.$transaction(async (tx) => {
      const account = await findOrCreateAccountByUsername(tx, username, role, autoCreate)
      if (!account) {
        throw new Error('ACCOUNT_NOT_FOUND')
      }

      const updated = await tx.account.update({
        where: { id: account.id },
        data: { balance: { increment: normalizedAmount } },
      })

      await tx.transaction.create({
        data: {
          accountId: account.id,
          amount: normalizedAmount,
          note,
          kind: 'manual-deposit',
          when: 'Just now',
        },
      })

      await tx.machineCashEvent.create({
        data: {
          direction: 'IN',
          amount: normalizedAmount,
          accountUsername: username,
          note,
          source,
        },
      })

      return {
        ok: true,
        username,
        balance: updated.balance,
      }
    })

    return NextResponse.json(result)
  } catch (e) {
    if (e instanceof Error && e.message === 'ACCOUNT_NOT_FOUND') {
      return NextResponse.json({ error: 'Account not found' }, { status: 404 })
    }
    const detail = JSON.stringify(e, Object.getOwnPropertyNames(e as object))
    return NextResponse.json({ error: 'DB error', detail }, { status: 500 })
  }
}
