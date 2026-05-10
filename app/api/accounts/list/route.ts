export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

export async function GET(request: Request) {
  const { searchParams } = new URL(request.url)
  const role = (searchParams.get('role') ?? '').trim().toLowerCase()

  const where = role ? { role } : undefined

  const accounts = await prisma.account.findMany({
    where,
    orderBy: { username: 'asc' },
    select: {
      username: true,
      role: true,
      balance: true,
      securityQuestion: true,
      createdAt: true,
    },
  })

  return NextResponse.json({ accounts })
}
