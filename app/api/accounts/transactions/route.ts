export const dynamic = 'force-dynamic'

import type { Prisma } from '@prisma/client'
import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'
import { getOwnedKidUsernames, getParentUsernameFromRequest } from '@/lib/parent-ownership'

export async function GET(request: Request) {
  const { searchParams } = new URL(request.url)
  const username = (searchParams.get('username') ?? '').trim().toLowerCase()
  const parentUsername = getParentUsernameFromRequest(request)

  let where: Prisma.TransactionWhereInput | undefined

  if (username) {
    where = {
      account: {
        username,
      },
    }
  } else if (parentUsername) {
    const ownedKids = await getOwnedKidUsernames(prisma, parentUsername)
    const visibleUsernames = [parentUsername, ...ownedKids]
    where = {
      account: {
        username: { in: visibleUsernames },
      },
    }
  }

  const rows = await prisma.transaction.findMany({
    where,
    orderBy: { createdAt: 'desc' },
    include: {
      account: {
        select: {
          username: true,
          role: true,
        },
      },
    },
    take: 200,
  })

  return NextResponse.json({
    transactions: rows.map((row) => ({
      id: row.id,
      child: row.account.username,
      amount: Math.abs(row.amount),
      signedAmount: row.amount,
      note: row.note,
      kind: row.kind,
      when: row.createdAt.toISOString(),
      role: row.account.role,
    })),
  })
}
