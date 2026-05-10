import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

export async function GET() {
  const inventory = await prisma.machineInventory.findUnique({
    where: { id: 1 },
  })

  const recentDeposits = await prisma.machineCashEvent.findMany({
    where: { direction: 'IN' },
    orderBy: { createdAt: 'desc' },
    take: 10,
  })

  const recentWithdrawals = await prisma.machineCashEvent.findMany({
    where: { direction: 'OUT' },
    orderBy: { createdAt: 'desc' },
    take: 10,
  })

  return NextResponse.json({
    currentInventory: inventory,
    recentDeposits: recentDeposits.map(d => ({
      amount: d.amount,
      source: d.source,
      createdAt: d.createdAt,
    })),
    recentWithdrawals: recentWithdrawals.map(w => ({
      amount: w.amount,
      breakdown: w.breakdown,
      createdAt: w.createdAt,
    })),
  })
}

export async function POST(request: Request) {
  const body = await request.json()

  if (body.action === 'reset') {
    await prisma.machineInventory.update({
      where: { id: 1 },
      data: {
        bill20: 0,
        bill50: 0,
        bill100: 0,
        bill500: 0,
        bill1000: 0,
        coin1: 0,
        coin5: 0,
        coin10: 0,
        coin20: 0,
      },
    })

    return NextResponse.json({ status: 'reset complete' })
  }

  return NextResponse.json({ error: 'Unknown action' }, { status: 400 })
}
