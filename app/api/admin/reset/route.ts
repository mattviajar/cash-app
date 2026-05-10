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
  const mode = String(payload.mode ?? 'soft') // 'soft' (keep accounts) or 'hard' (delete all)
  const confirm = String(payload.confirm ?? '')

  if (confirm !== 'RESET_ALL_DATA') {
    return NextResponse.json({
      error: 'Confirmation required',
      instructions: 'Send POST with { "mode": "soft", "confirm": "RESET_ALL_DATA" } to zero balances/inventory, or "mode": "hard" to delete all data'
    }, { status: 400 })
  }

  if (mode === 'soft') {
    // Soft reset: zero all balances and inventory, keep accounts and transaction history
    console.log('[RESET] Soft reset: zeroing all account balances and machine inventory')
    await prisma.$transaction(async (tx) => {
      // Zero all account balances
      await tx.account.updateMany({
        data: { balance: 0 },
      })

      // Zero machine inventory
      await tx.machineInventory.update({
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

      // Clear queues and pending items
      await tx.commandQueue.deleteMany({})
      await tx.depositQueue.deleteMany({})
      await tx.pendingWithdrawal.deleteMany({})
    })

    return NextResponse.json({
      status: 'soft reset complete',
      message: 'All account balances and machine inventory zeroed, accounts preserved'
    })
  }

  if (mode === 'hard') {
    // Hard reset: delete everything
    console.log('[RESET] Hard reset: deleting all data')
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

    return NextResponse.json({
      status: 'hard reset complete',
      message: 'All data deleted, machine ready for fresh start'
    })
  }

  return NextResponse.json({
    error: 'Invalid mode',
    message: 'Use "soft" to zero balances/inventory or "hard" to delete all data'
  }, { status: 400 })
}
