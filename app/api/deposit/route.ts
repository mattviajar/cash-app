export const dynamic = 'force-dynamic'
import { NextResponse } from 'next/server'
import type { Prisma } from '@prisma/client'
import { prisma } from '@/lib/prisma'
import {
  applyDepositToInventory,
  buildBreakdownFromField,
  normalizeCashAmount,
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

// Producer endpoint: hardware bridge posts deposits here.
export async function POST(request: Request) {
  let body: unknown
  try {
    body = await request.json()
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 })
  }

  const amount = Number((body as Record<string, unknown>).amount)
  if (!Number.isFinite(amount) || amount <= 0 || amount > 10000) {
    return NextResponse.json({ error: 'Invalid amount' }, { status: 400 })
  }

  const payload = body as Record<string, unknown>
  const username = typeof payload.account === 'string' ? payload.account.trim().toLowerCase() : ''
  const role = typeof payload.role === 'string' ? payload.role.trim().toLowerCase() : 'kid'
  const source = typeof payload.source === 'string' ? payload.source.trim().toLowerCase() : ''
  const note = typeof payload.note === 'string' ? payload.note.trim() : 'Hardware deposit'
  const autoCreate = payload.autoCreate !== false

  console.log(`[DEPOSIT] amount=${amount} source="${source}" account="${username}"`)

  try {
    const normalizedAmount = Math.round(amount * 100) / 100

    const result = await prisma.$transaction(async (tx) => {
      const item = await tx.depositQueue.create({
        data: { amount: normalizedAmount },
      })

      const inventoryField = await applyDepositToInventory(tx, normalizeCashAmount(normalizedAmount), source)
      const breakdown = buildBreakdownFromField(inventoryField)
      
      console.log(`[DEPOSIT] mapped field="${inventoryField}" breakdown=`, breakdown)

      let accountBalance: number | null = null
      if (username) {
        const account = await findOrCreateAccountByUsername(tx, username, role, autoCreate)
        if (!account) {
          throw new Error('ACCOUNT_NOT_FOUND')
        }

        const updated = await tx.account.update({
          where: { id: account.id },
          data: { balance: { increment: normalizedAmount } },
        })
        accountBalance = updated.balance

        await tx.transaction.create({
          data: {
            accountId: account.id,
            amount: normalizedAmount,
            note,
            kind: 'hardware-deposit',
            when: 'Just now',
          },
        })
      }

      await tx.machineCashEvent.create({
        data: {
          direction: 'IN',
          amount: normalizedAmount,
          accountUsername: username || null,
          note,
          source: source || 'hardware',
          breakdown: breakdown ?? undefined,
        },
      })

      return { item, inventoryField, accountBalance }
    })

    return NextResponse.json({ ok: true, ...result })
  } catch (e) {
    if (e instanceof Error && e.message === 'ACCOUNT_NOT_FOUND') {
      return NextResponse.json({ error: 'Account not found' }, { status: 404 })
    }
    const detail = JSON.stringify(e, Object.getOwnPropertyNames(e as object))
    return NextResponse.json({ error: 'DB error', detail }, { status: 500 })
  }
}

// Consumer endpoint: dashboard polls with ?since=<id> for new deposits.
export async function GET(request: Request) {
  const { searchParams } = new URL(request.url)
  const consume = searchParams.get('consume') === 'true'
  const sinceRaw = Number(searchParams.get('since') ?? '0')
  const sinceId = Number.isFinite(sinceRaw) && sinceRaw > 0 ? Math.floor(sinceRaw) : 0

  try {
    if (consume) {
      const deposits = await prisma.depositQueue.findMany({ orderBy: { id: 'asc' } })
      await prisma.depositQueue.deleteMany({})
      return NextResponse.json({ deposits })
    }

    const deposits = await prisma.depositQueue.findMany({
      where: { id: { gt: sinceId } },
      orderBy: { id: 'asc' },
    })
    return NextResponse.json({ deposits })
  } catch (e) {
    const detail = JSON.stringify(e, Object.getOwnPropertyNames(e as object))
    return NextResponse.json({ error: 'DB error', detail }, { status: 500 })
  }
}
