export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'
import { hashPassword } from '@/lib/password'

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

  if (!username || password.length < 6 || !securityQuestion || !securityAnswer) {
    return NextResponse.json({ error: 'Missing required fields' }, { status: 400 })
  }

  const parentExists = await prisma.account.findFirst({ where: { role: 'parent' } })
  if (parentExists) {
    return NextResponse.json({ error: 'Parent account already exists' }, { status: 409 })
  }

  try {
    const account = await prisma.account.create({
      data: {
        username,
        passwordHash: hashPassword(password),
        role: 'parent',
        securityQuestion,
        securityAnswer,
        balance: 0,
      },
      select: {
        username: true,
        role: true,
        balance: true,
      },
    })

    return NextResponse.json({ ok: true, account })
  } catch {
    return NextResponse.json({ error: 'Failed to create parent account' }, { status: 500 })
  }
}
