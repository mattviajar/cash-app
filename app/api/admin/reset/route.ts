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

export async function POST(request: Request) {
  if (!isAuthorized(request)) {
    return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })
  }

  let body: unknown
  try {
    body = await request.json()
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 })
  }

  const payload = body as Record<string, unknown>
  const confirm = typeof payload.confirm === 'string' ? payload.confirm.trim() : ''
  if (confirm !== 'RESET_ALL_DATA') {
    return NextResponse.json({ error: 'Confirmation required: confirm="RESET_ALL_DATA"' }, { status: 400 })
  }

  try {
    const result = await prisma.$transaction(async (tx) => {
      // Clear dependent relational records first.
      const deletedTransactions = await tx.transaction.deleteMany({})
      const deletedGoals = await tx.goal.deleteMany({})
      const deletedParentSettings = await tx.parentSettings.deleteMany({})
      const deletedPendingWithdrawals = await tx.pendingWithdrawal.deleteMany({})

      // Clear queue/event tables.
      const deletedDepositQueue = await tx.depositQueue.deleteMany({})
      const deletedCommandQueue = await tx.commandQueue.deleteMany({})
      const deletedMachineCashEvents = await tx.machineCashEvent.deleteMany({})

      // Clear auxiliary SQL tables when present.
      await tx.$executeRawUnsafe(`
        CREATE TABLE IF NOT EXISTS "ParentKidLink" (
          parent_username TEXT NOT NULL,
          kid_username TEXT NOT NULL UNIQUE,
          created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
          PRIMARY KEY (parent_username, kid_username)
        )
      `)
      await tx.$executeRawUnsafe(`
        CREATE TABLE IF NOT EXISTS "AccountEmail" (
          account_id TEXT PRIMARY KEY,
          email TEXT,
          created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
          updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
        )
      `)
      await tx.$executeRawUnsafe('DELETE FROM "ParentKidLink"')
      await tx.$executeRawUnsafe('DELETE FROM "AccountEmail"')

      const deletedAccounts = await tx.account.deleteMany({})

      // Reset machine lock and inventory to known empty state.
      const inventory = await tx.machineInventory.upsert({
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
        create: {
          id: 1,
          holder: null,
          mode: null,
          acquiredAt: null,
          expiresAt: null,
        },
        update: {
          holder: null,
          mode: null,
          acquiredAt: null,
          expiresAt: null,
        },
      })

      return {
        deletedAccounts: deletedAccounts.count,
        deletedTransactions: deletedTransactions.count,
        deletedGoals: deletedGoals.count,
        deletedParentSettings: deletedParentSettings.count,
        deletedPendingWithdrawals: deletedPendingWithdrawals.count,
        deletedDepositQueue: deletedDepositQueue.count,
        deletedCommandQueue: deletedCommandQueue.count,
        deletedMachineCashEvents: deletedMachineCashEvents.count,
        inventoryId: inventory.id,
      }
    })

    return NextResponse.json({ ok: true, ...result })
  } catch (error) {
    const message = error instanceof Error ? error.message : 'Unknown reset error'
    return NextResponse.json({ error: 'Reset failed', detail: message }, { status: 500 })
  }
}
