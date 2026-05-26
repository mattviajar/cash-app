import type { Prisma } from '@prisma/client'

const MACHINE_INVENTORY_ID = 1

const BILL_VALUES = [1000, 500, 100, 50, 20] as const
const COIN_VALUES = [20, 10, 5, 1] as const

type BillValue = (typeof BILL_VALUES)[number]
type CoinValue = (typeof COIN_VALUES)[number]

export type InventoryField =
  | 'bill20'
  | 'bill50'
  | 'bill100'
  | 'bill500'
  | 'bill1000'
  | 'coin1'
  | 'coin5'
  | 'coin10'
  | 'coin20'

export type InventoryBreakdown = Record<InventoryField, number>

type InventoryLike = {
  bill20: number
  bill50: number
  bill100: number
  bill500: number
  bill1000: number
  coin1: number
  coin5: number
  coin10: number
  coin20: number
}

const ZERO_BREAKDOWN: InventoryBreakdown = {
  bill20: 0,
  bill50: 0,
  bill100: 0,
  bill500: 0,
  bill1000: 0,
  coin1: 0,
  coin5: 0,
  coin10: 0,
  coin20: 0,
}

function billField(value: BillValue): InventoryField {
  switch (value) {
    case 20:
      return 'bill20'
    case 50:
      return 'bill50'
    case 100:
      return 'bill100'
    case 500:
      return 'bill500'
    case 1000:
      return 'bill1000'
  }
}

function coinField(value: CoinValue): InventoryField {
  switch (value) {
    case 1:
      return 'coin1'
    case 5:
      return 'coin5'
    case 10:
      return 'coin10'
    case 20:
      return 'coin20'
  }
}

export function parseInventoryField(value: string): InventoryField | null {
  switch (value.trim().toLowerCase()) {
    case 'bill20': return 'bill20'
    case 'bill50': return 'bill50'
    case 'bill100': return 'bill100'
    case 'bill500': return 'bill500'
    case 'bill1000': return 'bill1000'
    case 'coin1': return 'coin1'
    case 'coin5': return 'coin5'
    case 'coin10': return 'coin10'
    case 'coin20': return 'coin20'
    default:
      return null
  }
}

export function inventoryFieldValue(field: InventoryField): number {
  switch (field) {
    case 'bill20': return 20
    case 'bill50': return 50
    case 'bill100': return 100
    case 'bill500': return 500
    case 'bill1000': return 1000
    case 'coin1': return 1
    case 'coin5': return 5
    case 'coin10': return 10
    case 'coin20': return 20
  }
}

export function inventoryFieldLabel(field: InventoryField): string {
  switch (field) {
    case 'bill20': return '20 bill'
    case 'bill50': return '50 bill'
    case 'bill100': return '100 bill'
    case 'bill500': return '500 bill'
    case 'bill1000': return '1000 bill'
    case 'coin1': return '1 coin'
    case 'coin5': return '5 coin'
    case 'coin10': return '10 coin'
    case 'coin20': return '20 coin'
  }
}

export async function getOrCreateMachineInventory(
  tx: Prisma.TransactionClient
) {
  return tx.machineInventory.upsert({
    where: { id: MACHINE_INVENTORY_ID },
    create: { id: MACHINE_INVENTORY_ID },
    update: {},
  })
}

export function normalizeCashAmount(amount: number): number {
  return Math.round(amount)
}

export function inferDepositField(amount: number, source?: string): InventoryField | null {
  const normalized = normalizeCashAmount(amount)
  const normalizedSource = (source ?? '').trim().toLowerCase()

  if (normalizedSource === 'bill' && BILL_VALUES.includes(normalized as BillValue)) {
    return billField(normalized as BillValue)
  }
  if (normalizedSource === 'coin' && COIN_VALUES.includes(normalized as CoinValue)) {
    return coinField(normalized as CoinValue)
  }

  // Unknown source: only auto-classify non-ambiguous amounts.
  if (BILL_VALUES.includes(normalized as BillValue) && normalized !== 20) {
    return billField(normalized as BillValue)
  }
  if (COIN_VALUES.includes(normalized as CoinValue) && normalized !== 20) {
    return coinField(normalized as CoinValue)
  }

  return null
}

export async function applyDepositToInventory(
  tx: Prisma.TransactionClient,
  amount: number,
  source?: string
): Promise<InventoryField | null> {
  const inventory = await getOrCreateMachineInventory(tx)
  const field = inferDepositField(amount, source)

  if (!field) {
    return null
  }

  await tx.machineInventory.update({
    where: { id: inventory.id },
    data: {
      [field]: { increment: 1 },
    },
  })

  return field
}

type PlanEntry = {
  field: InventoryField
  value: number
  count: number
}

function toPlanEntries(inventory: InventoryLike): PlanEntry[] {
  return [
    { field: 'bill1000', value: 1000, count: inventory.bill1000 },
    { field: 'bill500', value: 500, count: inventory.bill500 },
    { field: 'bill100', value: 100, count: inventory.bill100 },
    { field: 'bill50', value: 50, count: inventory.bill50 },
    { field: 'bill20', value: 20, count: inventory.bill20 },
    { field: 'coin20', value: 20, count: inventory.coin20 },
    { field: 'coin10', value: 10, count: inventory.coin10 },
    { field: 'coin5', value: 5, count: inventory.coin5 },
    { field: 'coin1', value: 1, count: inventory.coin1 },
  ]
}

export function planWithdrawalBreakdown(
  inventory: InventoryLike,
  requestedAmount: number
): InventoryBreakdown | null {
  const amount = normalizeCashAmount(requestedAmount)
  if (!Number.isFinite(amount) || amount <= 0) {
    return null
  }

  const entries = toPlanEntries(inventory)
  const memo = new Map<string, InventoryBreakdown | null>()

  const dfs = (index: number, remaining: number): InventoryBreakdown | null => {
    const key = `${index}:${remaining}`
    if (memo.has(key)) {
      return memo.get(key) ?? null
    }

    if (remaining === 0) {
      memo.set(key, { ...ZERO_BREAKDOWN })
      return memo.get(key) ?? null
    }

    if (index >= entries.length || remaining < 0) {
      memo.set(key, null)
      return null
    }

    const entry = entries[index]
    const maxUse = Math.min(entry.count, Math.floor(remaining / entry.value))

    for (let use = maxUse; use >= 0; --use) {
      const sub = dfs(index + 1, remaining - use * entry.value)
      if (!sub) {
        continue
      }
      const merged: InventoryBreakdown = { ...sub }
      merged[entry.field] = (merged[entry.field] ?? 0) + use
      memo.set(key, merged)
      return merged
    }

    memo.set(key, null)
    return null
  }

  return dfs(0, amount)
}

export function buildInventoryDecrementData(plan: InventoryBreakdown): Prisma.MachineInventoryUpdateInput {
  const data: Prisma.MachineInventoryUpdateInput = {}

  for (const [field, count] of Object.entries(plan) as Array<[InventoryField, number]>) {
    if (count > 0) {
      data[field] = { decrement: count }
    }
  }

  return data
}

export function buildBreakdownFromField(field: InventoryField | null): InventoryBreakdown | null {
  if (!field) {
    return null
  }
  const breakdown = { ...ZERO_BREAKDOWN }
  breakdown[field] = 1
  return breakdown
}

export function buildExactBreakdown(field: InventoryField, count: number): InventoryBreakdown | null {
  const normalizedCount = Math.max(1, Math.round(count))
  if (!Number.isFinite(normalizedCount) || normalizedCount <= 0) {
    return null
  }
  const breakdown = { ...ZERO_BREAKDOWN }
  breakdown[field] = normalizedCount
  return breakdown
}
