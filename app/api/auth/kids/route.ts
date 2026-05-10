export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'
import { hashPassword } from '@/lib/password'

export async function GET() {
  const kids = await prisma.account.findMany({
    where: { role: 'kid' },
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

  if (!username || password.length < 4 || !securityQuestion || !securityAnswer) {
    return NextResponse.json({ error: 'Missing required fields' }, { status: 400 })
  }

  const existing = await prisma.account.findUnique({ where: { username } })
  if (existing) {
    return NextResponse.json({ error: 'Username already exists' }, { status: 409 })
  }

  const kid = await prisma.account.create({
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

  return NextResponse.json({ ok: true, kid })
}

export async function DELETE(request: Request) {
  const { searchParams } = new URL(request.url)
  const username = (searchParams.get('username') ?? '').trim().toLowerCase()

  if (!username) {
    return NextResponse.json({ error: 'Missing username' }, { status: 400 })
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
    await tx.account.delete({ where: { id: account.id } })
  })

  return NextResponse.json({ ok: true })
}
