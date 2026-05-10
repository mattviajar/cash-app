export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

export async function GET(request: Request) {
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
      balance: true,
      createdAt: true,
    },
  })

  if (!account) {
    return NextResponse.json({ error: 'Account not found' }, { status: 404 })
  }

  return NextResponse.json({ account })
}
