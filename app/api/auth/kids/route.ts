export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'
import { hashPassword } from '@/lib/password'
import {
  getOwnedKidUsernames,
  getParentUsernameFromRequest,
  isKidOwnedByParent,
  linkKidToParent,
  unlinkKidFromAnyParent,
  unlinkKidFromParent,
} from '@/lib/parent-ownership'

export async function GET(request: Request) {
  const parentUsername = getParentUsernameFromRequest(request)
  const { searchParams } = new URL(request.url)
  const username = (searchParams.get('username') ?? '').trim().toLowerCase()

  let where: { role: 'kid'; username?: string | { in: string[] } } = { role: 'kid' }

  if (parentUsername) {
    const ownedKids = await getOwnedKidUsernames(prisma, parentUsername)
    where = {
      role: 'kid',
      username: ownedKids.length > 0 ? { in: ownedKids } : { in: ['__none__'] },
    }
  } else if (username) {
    where = { role: 'kid', username }
  }

  const kids = await prisma.account.findMany({
    where,
    orderBy: { username: 'asc' },
    select: {
      username: true,
      balance: true,
      securityQuestion: true,
      createdAt: true,
    },
  })

  return NextResponse.json({ kids })
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
  const password = typeof payload.password === 'string' ? payload.password : ''
  const securityQuestion = typeof payload.securityQuestion === 'string' ? payload.securityQuestion.trim() : ''
  const securityAnswer = typeof payload.securityAnswer === 'string' ? payload.securityAnswer.trim().toLowerCase() : ''
  const parentUsername =
    typeof payload.parentUsername === 'string'
      ? payload.parentUsername.trim().toLowerCase()
      : getParentUsernameFromRequest(request)

  if (!username || password.length < 4 || !securityQuestion || !securityAnswer) {
    return NextResponse.json({ error: 'Missing required fields' }, { status: 400 })
  }
  if (!parentUsername) {
    return NextResponse.json({ error: 'Missing parent username' }, { status: 400 })
  }

  const existing = await prisma.account.findUnique({ where: { username } })
  if (existing) {
    return NextResponse.json({ error: 'Username already exists' }, { status: 409 })
  }

  try {
    const kid = await prisma.$transaction(async (tx) => {
      const created = await tx.account.create({
        data: {
          username,
          passwordHash: hashPassword(password),
          role: 'kid',
          securityQuestion,
          securityAnswer,
          balance: 0,
        },
        select: {
          username: true,
          balance: true,
          securityQuestion: true,
        },
      })

      await linkKidToParent(tx, parentUsername, username)
      return created
    })

    return NextResponse.json({ ok: true, kid })
  } catch (error) {
    if (error instanceof Error && error.message === 'KID_ALREADY_OWNED_BY_ANOTHER_PARENT') {
      return NextResponse.json({ error: 'Kid account is already linked to another parent' }, { status: 409 })
    }
    return NextResponse.json({ error: 'Failed to create kid account' }, { status: 500 })
  }
}

export async function DELETE(request: Request) {
  const { searchParams } = new URL(request.url)
  const username = (searchParams.get('username') ?? '').trim().toLowerCase()
  const parentUsername = getParentUsernameFromRequest(request)

  if (!username) {
    return NextResponse.json({ error: 'Missing username' }, { status: 400 })
  }
  if (!parentUsername) {
    return NextResponse.json({ error: 'Missing parent username' }, { status: 400 })
  }

  const owned = await isKidOwnedByParent(prisma, parentUsername, username)
  if (!owned) {
    return NextResponse.json({ error: 'You can only delete your own kid accounts' }, { status: 403 })
  }

  const account = await prisma.account.findUnique({ where: { username } })
  if (!account || account.role !== 'kid') {
    return NextResponse.json({ error: 'Kid account not found' }, { status: 404 })
  }

  await prisma.$transaction(async (tx) => {
    await tx.transaction.deleteMany({ where: { accountId: account.id } })
    await tx.goal.deleteMany({ where: { accountId: account.id } })
    await tx.parentSettings.deleteMany({ where: { accountId: account.id } })
    await tx.pendingWithdrawal.deleteMany({ where: { child: username } })
    await unlinkKidFromParent(tx, parentUsername, username)
    await unlinkKidFromAnyParent(tx, username)
    await tx.account.delete({ where: { id: account.id } })
  })

  return NextResponse.json({ ok: true })
}
