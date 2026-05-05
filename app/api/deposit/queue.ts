export type DepositItem = { id: number; amount: number }

// Temporary in-memory queue for coin deposits.
// Good for demos; resets when the dev server restarts.
const depositQueue: DepositItem[] = []
const MAX_QUEUE_ITEMS = 500
let nextId = 1

export function enqueueDeposit(amount: number): DepositItem {
  const normalizedAmount = Math.round(amount * 100) / 100
  const item: DepositItem = { id: nextId++, amount: normalizedAmount }
  depositQueue.push(item)
  if (depositQueue.length > MAX_QUEUE_ITEMS) {
    depositQueue.splice(0, depositQueue.length - MAX_QUEUE_ITEMS)
  }
  return item
}

export function snapshotDeposits(sinceId = 0): DepositItem[] {
  if (sinceId <= 0) {
    return [...depositQueue]
  }

  const startIndex = depositQueue.findIndex((item) => item.id > sinceId)
  if (startIndex < 0) {
    return []
  }

  return depositQueue.slice(startIndex)
}

export function consumeDeposits(): DepositItem[] {
  return depositQueue.splice(0)
}
