export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'
import { verifyPassword } from '@/lib/password'

// Single-tenant lockdown. Only usernames in this allowlist may log in.
// Override at deploy time with env var: ALLOWED_USERNAMES="matt,james,foo"
const ALLOWED_USERNAMES = (process.env.ALLOWED_USERNAMES ?? 'matt,james')
  .split(',')
  .map((u) => u.trim().toLowerCase())
  .filter(Boolean)

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
  const role = typeof payload.role === 'string' ? payload.role.trim().toLowerCase() : ''

  if (!username || !password || (role !== 'kid' && role !== 'parent')) {
    return NextResponse.json({ error: 'Invalid credentials' }, { status: 400 })
  }

  if (!ALLOWED_USERNAMES.includes(username)) {
    // Generic message so we don't leak which usernames exist.
    return NextResponse.json({ error: 'Incorrect username or password' }, { status: 401 })
  }

  const account = await prisma.account.findUnique({ where: { username } })
  if (!account || account.role !== role) {
    return NextResponse.json({ error: 'Incorrect username or password' }, { status: 401 })
  }

  if (!verifyPassword(password, account.passwordHash)) {
    return NextResponse.json({ error: 'Incorrect username or password' }, { status: 401 })
  }

  return NextResponse.json({
    ok: true,
    account: {
      username: account.username,
      role: account.role,
      balance: account.balance,
    },
  })
}
