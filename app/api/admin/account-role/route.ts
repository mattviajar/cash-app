export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

// Optional admin debug guard. If set, callers must provide
// x-admin-debug-token header that matches this value.
const ADMIN_DEBUG_TOKEN = process.env.ADMIN_DEBUG_TOKEN ?? ''

function isAuthorized(request: Request) {
  if (!ADMIN_DEBUG_TOKEN) return true
  return request.headers.get('x-admin-debug-token') === ADMIN_DEBUG_TOKEN
}

export async function GET(request: Request) {
  if (!isAuthorized(request)) {
    return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })
  }

  const { searchParams } = new URL(request.url)
  const username = (searchParams.get('username') ?? '').trim().toLowerCase()

  if (!username) {
    return NextResponse.json({ error: 'Missing username' }, { status: 400 })
  }

  const account = await prisma.account.findUnique({
    where: { username },
    select: {
      username: true,
      role: true,
      createdAt: true,
    },
  })

  if (!account) {
    return NextResponse.json({ exists: false, username })
  }

  return NextResponse.json({
    exists: true,
    account,
  })
}
