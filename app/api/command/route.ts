export const dynamic = 'force-dynamic'
import { NextResponse } from 'next/server'
import type { Prisma } from '@prisma/client'
import { prisma } from '@/lib/prisma'
import {
  buildInventoryDecrementData,
  normalizeCashAmount,
  planWithdrawalBreakdown,
} from '@/lib/machine-cash'

async function findOrCreateAccountByUsername(
  tx: Prisma.TransactionClient,
  username: string,
  role: string,
  autoCreate: boolean
) {
  const normalized = username.trim().toLowerCase()
  if (!normalized) {
    return null
  }

  const existing = await tx.account.findUnique({ where: { username: normalized } })
  if (existing) {
    return existing
  }

  if (!autoCreate) {
    return null
  }

  return tx.account.create({
    data: {
      username: normalized,
      passwordHash: 'legacy-local-account',
      role: role === 'parent' ? 'parent' : 'kid',
    },
  })
}

function parseWithdrawAmount(command: string): number | null {
  if (!/^\s*WITHDRAW\b/i.test(command)) {
    return null
  }
  const match = command.match(/(\d+)/)
  if (!match) {
    return 20
  }
  const value = Number(match[1])
  if (!Number.isFinite(value) || value <= 0) {
    return null
  }
  return normalizeCashAmount(value)
}

// Producer: dashboard posts commands here when a withdrawal is approved.
export async function POST(request: Request) {
  let body: unknown
  try {
    body = await request.json()
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 })
  }

  const payload = body as Record<string, unknown>
  const cmd = String(payload.command ?? '').trim()
  if (!cmd || cmd.length > 128) {
    return NextResponse.json({ error: 'Missing or invalid command' }, { status: 400 })
  }

  const username = typeof payload.account === 'string' ? payload.account.trim().toLowerCase() : ''
  const lockOwner = typeof payload.lockOwner === 'string' ? payload.lockOwner.trim().toLowerCase() : username
  const role = typeof payload.role === 'string' ? payload.role.trim().toLowerCase() : 'kid'
  const autoCreate = payload.autoCreate !== false
  const note = typeof payload.note === 'string' ? payload.note.trim() : 'Machine withdrawal'

  const withdrawAmount = parseWithdrawAmount(cmd)

  if (withdrawAmount !== null) {
    try {
      const result = await prisma.$transaction(async (tx) => {
        const lock = await tx.deviceLock.upsert({
          where: { id: 1 },
          create: { id: 1 },
          update: {},
        })

        const lockActive = !!lock.holder && !!lock.expiresAt && lock.expiresAt.getTime() > Date.now()
        if (!lockActive) {
          throw new Error('DEVICE_NOT_LOCKED')
        }
        if (lockOwner && lock.holder !== lockOwner) {
          throw new Error('DEVICE_LOCK_MISMATCH')
        }

        const inventory = await tx.machineInventory.upsert({
          where: { id: 1 },
          create: { id: 1 },
          update: {},
        })

        console.log(`[WITHDRAW] amount=${withdrawAmount} inventory=`, inventory)
        
        const plan = planWithdrawalBreakdown(inventory, withdrawAmount)
        if (!plan) {
          throw new Error('INSUFFICIENT_INVENTORY')
        }
        
        console.log(`[WITHDRAW] plan generated=`, plan)

        const decrementData = buildInventoryDecrementData(plan)
        await tx.machineInventory.update({
          where: { id: inventory.id },
          data: decrementData,
        })

        let accountBalance: number | null = null
        if (username) {
          const account = await findOrCreateAccountByUsername(tx, username, role, autoCreate)
          if (!account) {
            throw new Error('ACCOUNT_NOT_FOUND')
          }
          if (account.balance < withdrawAmount) {
            throw new Error('INSUFFICIENT_ACCOUNT_BALANCE')
          }

          const updated = await tx.account.update({
            where: { id: account.id },
            data: { balance: { decrement: withdrawAmount } },
          })
          accountBalance = updated.balance

          await tx.transaction.create({
            data: {
              accountId: account.id,
              amount: -Math.abs(withdrawAmount),
              note,
              kind: 'machine-withdrawal',
              when: 'Just now',
            },
          })
        }

        await tx.machineCashEvent.create({
          data: {
            direction: 'OUT',
            amount: withdrawAmount,
            accountUsername: username || null,
            note,
            source: 'dashboard',
            breakdown: plan,
          },
        })

        // Send breakdown to firmware so it knows which motors to use
        // Format: WITHDRAW <amount> [coin20=N coin10=N ...]
        let cmdStr = `WITHDRAW ${withdrawAmount}`
        if (plan.coin20 > 0) cmdStr += ` coin20=${plan.coin20}`
        if (plan.coin10 > 0) cmdStr += ` coin10=${plan.coin10}`
        if (plan.coin5 > 0) cmdStr += ` coin5=${plan.coin5}`
        if (plan.coin1 > 0) cmdStr += ` coin1=${plan.coin1}`
        if (plan.bill20 > 0) cmdStr += ` bill20=${plan.bill20}`
        if (plan.bill50 > 0) cmdStr += ` bill50=${plan.bill50}`
        if (plan.bill100 > 0) cmdStr += ` bill100=${plan.bill100}`
        if (plan.bill500 > 0) cmdStr += ` bill500=${plan.bill500}`
        if (plan.bill1000 > 0) cmdStr += ` bill1000=${plan.bill1000}`

        await tx.commandQueue.create({ data: { command: cmdStr } })
        return { ok: true, amount: withdrawAmount, plan, accountBalance }
      })

      return NextResponse.json(result)
    } catch (e) {
      if (e instanceof Error && e.message === 'INSUFFICIENT_INVENTORY') {
        return NextResponse.json({ error: 'Insufficient machine cash inventory' }, { status: 409 })
      }
      if (e instanceof Error && e.message === 'ACCOUNT_NOT_FOUND') {
        return NextResponse.json({ error: 'Account not found' }, { status: 404 })
      }
      if (e instanceof Error && e.message === 'INSUFFICIENT_ACCOUNT_BALANCE') {
        return NextResponse.json({ error: 'Insufficient account balance' }, { status: 409 })
      }
      if (e instanceof Error && e.message === 'DEVICE_NOT_LOCKED') {
        return NextResponse.json({ error: 'Device lock required before withdrawal' }, { status: 409 })
      }
      if (e instanceof Error && e.message === 'DEVICE_LOCK_MISMATCH') {
        return NextResponse.json({ error: 'Device is currently locked by another user' }, { status: 409 })
      }
      const detail = JSON.stringify(e, Object.getOwnPropertyNames(e as object))
      return NextResponse.json({ error: 'DB error', detail }, { status: 500 })
    }
  }

  await prisma.commandQueue.create({ data: { command: cmd } })
  return NextResponse.json({ ok: true })
}

// Consumer: serial bridge polls this to drain and send commands to ESP32.
export async function GET(request: Request) {
  const { searchParams } = new URL(request.url)
  const consume = searchParams.get('consume') === 'true'

  if (consume) {
    const rows = await prisma.commandQueue.findMany({ orderBy: { id: 'asc' } })
    await prisma.commandQueue.deleteMany({})
    return NextResponse.json({ commands: rows.map((c) => c.command) })
  }

  const rows = await prisma.commandQueue.findMany({ orderBy: { id: 'asc' } })
  return NextResponse.json({ commands: rows.map((c) => c.command) })
}
