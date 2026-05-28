export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import type { Prisma, PrismaClient } from '@prisma/client'
import { prisma } from '@/lib/prisma'
import { hashPassword } from '@/lib/password'
import {
  SESSION_COOKIE_NAME,
  createSessionToken,
  getSessionFromRequest,
} from '@/lib/session'

type DbClient = PrismaClient | Prisma.TransactionClient

type AccountEmailRow = {
  account_id: string
  email: string | null
}

async function ensureAccountEmailTable(db: DbClient) {
  await db.$executeRawUnsafe(`
    CREATE TABLE IF NOT EXISTS "AccountEmail" (
      account_id TEXT PRIMARY KEY,
      email TEXT,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      CONSTRAINT accountemail_account_fk
        FOREIGN KEY (account_id)
        REFERENCES "Account"(id)
        ON DELETE CASCADE
    )
  `)
}

async function getAccountEmail(db: DbClient, accountId: string): Promise<string> {
  await ensureAccountEmailTable(db)
  const rows = await db.$queryRawUnsafe<AccountEmailRow[]>(
    'SELECT account_id, email FROM "AccountEmail" WHERE account_id = $1 LIMIT 1',
    accountId
  )
  return (rows[0]?.email ?? '').trim()
}

async function upsertAccountEmail(db: DbClient, accountId: string, email: string) {
  await ensureAccountEmailTable(db)
  await db.$executeRawUnsafe(
    `
      INSERT INTO "AccountEmail" (account_id, email, updated_at)
      VALUES ($1, $2, NOW())
      ON CONFLICT (account_id)
      DO UPDATE SET email = EXCLUDED.email, updated_at = NOW()
    `,
    accountId,
    email
  )
}

function isValidEmail(value: string): boolean {
  if (!value) return true
  return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value)
}

export async function GET(request: Request) {
  const session = getSessionFromRequest(request)
  if (!session) {
    return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })
  }

  const account = await prisma.account.findUnique({
    where: { username: session.username },
    select: {
      id: true,
      username: true,
      role: true,
      securityQuestion: true,
    },
  })

  if (!account || account.role !== session.role) {
    return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })
  }

  const email = await getAccountEmail(prisma, account.id)

  return NextResponse.json({
    account: {
      username: account.username,
      role: account.role,
      email,
      securityQuestion: account.securityQuestion ?? '',
    },
  })
}

export async function PATCH(request: Request) {
  const session = getSessionFromRequest(request)
  if (!session) {
    return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })
  }

  let body: unknown
  try {
    body = await request.json()
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 })
  }

  const payload = body as Record<string, unknown>

  const requestedUsername = typeof payload.username === 'string'
    ? payload.username.trim().toLowerCase()
    : undefined
  const requestedEmail = typeof payload.email === 'string'
    ? payload.email.trim().toLowerCase()
    : undefined
  const requestedPassword = typeof payload.password === 'string'
    ? payload.password
    : undefined
  const requestedSecurityQuestion = typeof payload.securityQuestion === 'string'
    ? payload.securityQuestion.trim()
    : undefined

  if (
    requestedUsername === undefined
    && requestedEmail === undefined
    && requestedPassword === undefined
    && requestedSecurityQuestion === undefined
  ) {
    return NextResponse.json({ error: 'No updates provided' }, { status: 400 })
  }

  if (requestedUsername !== undefined && requestedUsername.length === 0) {
    return NextResponse.json({ error: 'Username is required' }, { status: 400 })
  }

  if (requestedPassword !== undefined && requestedPassword.length > 0 && requestedPassword.length < 4) {
    return NextResponse.json({ error: 'Password must be at least 4 characters' }, { status: 400 })
  }

  if (requestedSecurityQuestion !== undefined && requestedSecurityQuestion.length === 0) {
    return NextResponse.json({ error: 'Security question cannot be empty' }, { status: 400 })
  }

  if (requestedEmail !== undefined && !isValidEmail(requestedEmail)) {
    return NextResponse.json({ error: 'Invalid Gmail/email format' }, { status: 400 })
  }

  const nowSec = Math.floor(Date.now() / 1000)
  const remainingSessionSeconds = Math.max(60, session.exp - nowSec)

  try {
    const updated = await prisma.$transaction(async (tx) => {
      const account = await tx.account.findUnique({
        where: { username: session.username },
        select: {
          id: true,
          username: true,
          role: true,
          securityQuestion: true,
        },
      })

      if (!account || account.role !== session.role) {
        throw new Error('UNAUTHORIZED')
      }

      const nextUsername = requestedUsername ?? account.username

      if (nextUsername !== account.username) {
        const existing = await tx.account.findUnique({
          where: { username: nextUsername },
          select: { id: true },
        })
        if (existing) {
          throw new Error('USERNAME_TAKEN')
        }
      }

      const updateData: {
        username?: string
        passwordHash?: string
        securityQuestion?: string
      } = {}

      if (nextUsername !== account.username) {
        updateData.username = nextUsername
      }

      if (requestedPassword && requestedPassword.length > 0) {
        updateData.passwordHash = hashPassword(requestedPassword)
      }

      if (requestedSecurityQuestion !== undefined) {
        updateData.securityQuestion = requestedSecurityQuestion
      }

      const updatedAccount = Object.keys(updateData).length > 0
        ? await tx.account.update({
          where: { username: account.username },
          data: updateData,
          select: {
            id: true,
            username: true,
            role: true,
            securityQuestion: true,
          },
        })
        : account

      if (requestedEmail !== undefined) {
        await upsertAccountEmail(tx, updatedAccount.id, requestedEmail)
      }

      if (account.username !== updatedAccount.username) {
        if (updatedAccount.role === 'parent') {
          await tx.$executeRawUnsafe(
            'UPDATE "ParentKidLink" SET parent_username = $1 WHERE parent_username = $2',
            updatedAccount.username,
            account.username
          )
        } else {
          await tx.$executeRawUnsafe(
            'UPDATE "ParentKidLink" SET kid_username = $1 WHERE kid_username = $2',
            updatedAccount.username,
            account.username
          )

          await tx.pendingWithdrawal.updateMany({
            where: { child: account.username },
            data: { child: updatedAccount.username },
          })
        }

        await tx.deviceLock.updateMany({
          where: { holder: account.username },
          data: { holder: updatedAccount.username },
        })
      }

      const email = requestedEmail !== undefined
        ? requestedEmail
        : await getAccountEmail(tx, updatedAccount.id)

      return {
        username: updatedAccount.username,
        role: updatedAccount.role,
        securityQuestion: updatedAccount.securityQuestion ?? '',
        email,
      }
    })

    const response = NextResponse.json({ ok: true, account: updated })
    response.cookies.set({
      name: SESSION_COOKIE_NAME,
      value: createSessionToken(updated.username, updated.role as 'kid' | 'parent', remainingSessionSeconds),
      httpOnly: true,
      secure: process.env.NODE_ENV === 'production',
      sameSite: 'lax',
      path: '/',
      maxAge: remainingSessionSeconds,
    })

    return response
  } catch (error) {
    if (error instanceof Error) {
      if (error.message === 'UNAUTHORIZED') {
        return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })
      }
      if (error.message === 'USERNAME_TAKEN') {
        return NextResponse.json({ error: 'Username already taken' }, { status: 409 })
      }
    }

    return NextResponse.json({ error: 'Failed to update profile' }, { status: 500 })
  }
}
