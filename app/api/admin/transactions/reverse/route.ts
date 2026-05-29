export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

const ADMIN_DEBUG_TOKEN = process.env.ADMIN_DEBUG_TOKEN ?? ''

function isAuthorized(request: Request) {
  if (!ADMIN_DEBUG_TOKEN) return true
  return request.headers.get('x-admin-debug-token') === ADMIN_DEBUG_TOKEN
}

const REVERSIBLE_KINDS = new Set(['manual-deposit', 'hardware-deposit'])

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
  const transactionIdRaw = Number(payload.transactionId)
  const transactionId = Number.isInteger(transactionIdRaw) ? transactionIdRaw : NaN

  if (!Number.isInteger(transactionId) || transactionId <= 0) {
    return NextResponse.json({ error: 'Invalid transactionId' }, { status: 400 })
  }

  try {
    const result = await prisma.$transaction(async (tx) => {
      const original = await tx.transaction.findUnique({
        where: { id: transactionId },
        include: {
          account: {
            select: {
              id: true,
              username: true,
              balance: true,
            },
          },
        },
      })

      if (!original) {
        throw new Error('TRANSACTION_NOT_FOUND')
      }

      if (original.amount <= 0 || !REVERSIBLE_KINDS.has(original.kind)) {
        throw new Error('TRANSACTION_NOT_REVERSIBLE')
      }

      const reversalNote = `Reversal of deposit #${original.id}`
      const existingReversal = await tx.transaction.findFirst({
        where: {
          accountId: original.accountId,
          note: reversalNote,
          kind: 'admin-reversal',
        },
        select: { id: true },
      })

      if (existingReversal) {
        throw new Error('TRANSACTION_ALREADY_REVERSED')
      }

      const updatedAccount = await tx.account.update({
        where: { id: original.accountId },
        data: {
          balance: {
            decrement: original.amount,
          },
        },
        select: {
          username: true,
          balance: true,
        },
      })

      const reversal = await tx.transaction.create({
        data: {
          accountId: original.accountId,
          amount: -original.amount,
          note: reversalNote,
          kind: 'admin-reversal',
          when: 'Just now',
        },
      })

      await tx.machineCashEvent.create({
        data: {
          direction: 'ADJUST',
          amount: -original.amount,
          accountUsername: original.account.username,
          note: `${reversalNote} (${original.note})`,
          source: 'admin-rollback',
        },
      })

      return {
        ok: true,
        reversedTransactionId: original.id,
        reversalTransactionId: reversal.id,
        username: updatedAccount.username,
        newBalance: updatedAccount.balance,
        reversedAmount: original.amount,
      }
    })

    return NextResponse.json(result)
  } catch (error) {
    if (error instanceof Error) {
      if (error.message === 'TRANSACTION_NOT_FOUND') {
        return NextResponse.json({ error: 'Transaction not found' }, { status: 404 })
      }
      if (error.message === 'TRANSACTION_NOT_REVERSIBLE') {
        return NextResponse.json({ error: 'Only positive deposit transactions can be reversed' }, { status: 400 })
      }
      if (error.message === 'TRANSACTION_ALREADY_REVERSED') {
        return NextResponse.json({ error: 'Transaction was already reversed' }, { status: 409 })
      }
    }

    const detail = JSON.stringify(error, Object.getOwnPropertyNames(error as object))
    return NextResponse.json({ error: 'Failed to reverse transaction', detail }, { status: 500 })
  }
}