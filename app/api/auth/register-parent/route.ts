export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'
import { hashPassword } from '@/lib/password'

// Registration is enabled by default.
// Set REGISTRATION_ENABLED="false" at deploy time to disable it.
// Optional: set ALLOWED_USERNAMES (comma-separated) to restrict signups.
const REGISTRATION_ENABLED = process.env.REGISTRATION_ENABLED !== 'false'
const ALLOWED_USERNAMES = (process.env.ALLOWED_USERNAMES ?? '')
  .split(',')
  .map((u) => u.trim().toLowerCase())
  .filter(Boolean)

export async function POST(request: Request) {
  if (!REGISTRATION_ENABLED) {
    return NextResponse.json({ error: 'Registration is disabled' }, { status: 403 })
  }

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

  if (ALLOWED_USERNAMES.length > 0 && !ALLOWED_USERNAMES.includes(username)) {
    return NextResponse.json({ error: 'Username not permitted' }, { status: 403 })
  }

  const usernameTaken = await prisma.account.findUnique({ where: { username } })
  if (usernameTaken) {
    return NextResponse.json({ error: 'Username already taken' }, { status: 409 })
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
