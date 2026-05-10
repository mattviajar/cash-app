export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

export async function GET() {
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

  return NextResponse.json({
    inventory,
    totalValue,
  })
}
