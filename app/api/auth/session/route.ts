export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'
import { getSessionFromRequest } from '@/lib/session'

export async function GET(request: Request) {
  const session = getSessionFromRequest(request)
  if (!session) {
    return NextResponse.json({ authenticated: false }, { status: 401 })
  }

  const account = await prisma.account.findUnique({ where: { username: session.username } })
  if (!account) {
    return NextResponse.json({ authenticated: false }, { status: 401 })
  }

  if (account.role !== session.role) {
    return NextResponse.json({ authenticated: false }, { status: 401 })
  }

  return NextResponse.json({
    authenticated: true,
    account: {
      username: account.username,
      role: account.role,
      balance: account.balance,
    },
  })
}
