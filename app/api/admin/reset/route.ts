export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

export async function POST(request: Request) {
  let body: unknown
  try {
    body = await request.json()
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 })
  }

  const payload = body as Record<string, unknown>
  if (payload.confirm !== 'RESET_ALL_DATA') {
    return NextResponse.json({ error: 'Confirmation token mismatch' }, { status: 400 })
  }

  await prisma.$transaction(async (tx) => {
    await tx.commandQueue.deleteMany({})
    await tx.depositQueue.deleteMany({})
    await tx.pendingWithdrawal.deleteMany({})
    await tx.transaction.deleteMany({})
    await tx.goal.deleteMany({})
    await tx.parentSettings.deleteMany({})
    await tx.machineCashEvent.deleteMany({})
    await tx.account.deleteMany({})
    await tx.machineInventory.upsert({
      where: { id: 1 },
      create: {
        id: 1,
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
      update: {
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
    await tx.deviceLock.upsert({
      where: { id: 1 },
      create: { id: 1, holder: null, mode: null, acquiredAt: null, expiresAt: null },
      update: { holder: null, mode: null, acquiredAt: null, expiresAt: null },
    })
  })

  return NextResponse.json({ ok: true, message: 'All accounts, balances, inventory, and transactions reset.' })
}
