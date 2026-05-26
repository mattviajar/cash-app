export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'
import { getOwnedKidUsernames, getParentUsernameFromRequest } from '@/lib/parent-ownership'

export async function GET(request: Request) {
  const { searchParams } = new URL(request.url)
  const role = (searchParams.get('role') ?? '').trim().toLowerCase()
  const parentUsername = getParentUsernameFromRequest(request)

  const where: {
    role?: string
    username?: { in: string[] }
  } = {}

  if (role) {
    where.role = role
  }

  if (parentUsername && role !== 'parent') {
    const ownedKids = await getOwnedKidUsernames(prisma, parentUsername)
    where.username = { in: ownedKids.length > 0 ? ownedKids : ['__none__'] }
    where.role = 'kid'
  }

  const accounts = await prisma.account.findMany({
    where: Object.keys(where).length > 0 ? where : undefined,
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
