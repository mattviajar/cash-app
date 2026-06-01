export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'
import { getSessionFromRequest } from '@/lib/session'

export async function GET(request: Request) {
  const session = getSessionFromRequest(request)
  const inventory = await prisma.machineInventory.upsert({
    where: { id: 1 },
    create: { id: 1 },
    update: {},
  })

  const totalValue =
    inventory.bill1000 * 1000 +
    inventory.bill500 * 500 +
    inventory.bill100 * 100 +
    inventory.bill50 * 50 +
    inventory.bill20 * 20 +
    inventory.coin20 * 20 +
    inventory.coin10 * 10 +
    inventory.coin5 * 5 +
    inventory.coin1

  if (session?.role === 'kid') {
    return NextResponse.json({
      inventory: {
        bill1000: inventory.bill1000 > 0 ? 1 : 0,
        bill500: inventory.bill500 > 0 ? 1 : 0,
        bill100: inventory.bill100 > 0 ? 1 : 0,
        bill50: inventory.bill50 > 0 ? 1 : 0,
        bill20: inventory.bill20 > 0 ? 1 : 0,
        coin20: inventory.coin20 > 0 ? 1 : 0,
        coin10: inventory.coin10 > 0 ? 1 : 0,
        coin5: inventory.coin5 > 0 ? 1 : 0,
        coin1: inventory.coin1 > 0 ? 1 : 0,
      },
      totalValue: null,
    })
  }

  return NextResponse.json({
    inventory,
    totalValue,
  })
}
