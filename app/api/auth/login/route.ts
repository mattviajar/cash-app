export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'
import { verifyPassword } from '@/lib/password'
import {
  createSessionToken,
  LONG_SESSION_MAX_AGE_SECONDS,
  SESSION_COOKIE_NAME,
  SHORT_SESSION_MAX_AGE_SECONDS,
} from '@/lib/session'

// Optional allowlist. When ALLOWED_USERNAMES is empty or unset,
// any valid account may log in.
const ALLOWED_USERNAMES = (process.env.ALLOWED_USERNAMES ?? '')
  .split(',')
  .map((u) => u.trim().toLowerCase())
  .filter(Boolean)

const AUTH_DEBUG_LOGS = process.env.AUTH_DEBUG_LOGS !== 'false'

export async function POST(request: Request) {
  const authFail = (
    status: number,
    error: string,
    code: string,
    details: { username?: string; requestedRole?: string; actualRole?: string } = {}
  ) => {
    if (AUTH_DEBUG_LOGS) {
      console.warn('[auth/login] failed', {
        code,
        username: details.username ?? null,
        requestedRole: details.requestedRole ?? null,
        actualRole: details.actualRole ?? null,
      })
    }

    return NextResponse.json({ error, code }, { status })
  }

  let body: unknown
  try {
    body = await request.json()
  } catch {
    return authFail(400, 'Invalid JSON', 'INVALID_JSON')
  }

  const payload = body as Record<string, unknown>
  const username = typeof payload.username === 'string' ? payload.username.trim().toLowerCase() : ''
  const password = typeof payload.password === 'string' ? payload.password : ''
  const role = typeof payload.role === 'string' ? payload.role.trim().toLowerCase() : ''
  const rememberMe = payload.rememberMe === true

  if (!username || !password || (role !== 'kid' && role !== 'parent')) {
    return authFail(400, 'Invalid credentials', 'INVALID_CREDENTIALS_FORMAT', {
      username,
      requestedRole: role,
    })
  }

  if (ALLOWED_USERNAMES.length > 0 && !ALLOWED_USERNAMES.includes(username)) {
    // Generic message so we don't leak which usernames exist.
    return authFail(401, 'Incorrect username or password', 'USERNAME_NOT_ALLOWED', {
      username,
      requestedRole: role,
    })
  }

  const account = await prisma.account.findUnique({ where: { username } })
  if (!account) {
    return authFail(401, 'Incorrect username or password', 'USER_NOT_FOUND', {
      username,
      requestedRole: role,
    })
  }

  if (!verifyPassword(password, account.passwordHash)) {
    return authFail(401, 'Incorrect username or password', 'WRONG_PASSWORD', {
      username,
      requestedRole: role,
      actualRole: account.role,
    })
  }

  if (account.role !== role) {
    return authFail(
      401,
      `This account is registered as ${account.role}. Select ${account.role} and try again.`,
      'ACCOUNT_ROLE_MISMATCH',
      {
        username,
        requestedRole: role,
        actualRole: account.role,
      }
    )
  }

  const maxAge = rememberMe ? LONG_SESSION_MAX_AGE_SECONDS : SHORT_SESSION_MAX_AGE_SECONDS
  const sessionToken = createSessionToken(account.username, account.role as 'kid' | 'parent', maxAge)

  const response = NextResponse.json({
    ok: true,
    account: {
      username: account.username,
      role: account.role,
      balance: account.balance,
    },
  })

  response.cookies.set({
    name: SESSION_COOKIE_NAME,
    value: sessionToken,
    httpOnly: true,
    secure: process.env.NODE_ENV === 'production',
    sameSite: 'lax',
    path: '/',
    maxAge,
  })

  return response
}
