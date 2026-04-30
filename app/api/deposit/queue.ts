export type DepositItem = { id: number; amount: number }

// Temporary in-memory queue for coin deposits.
// Good for demos; resets when the dev server restarts.
const depositQueue: DepositItem[] = []
let nextId = 1

export function enqueueDeposit(amount: number): DepositItem {
  const normalizedAmount = Math.round(amount * 100) / 100
  const item: DepositItem = { id: nextId++, amount: normalizedAmount }
  depositQueue.push(item)
  return item
}

export function snapshotDeposits(): DepositItem[] {
  return [...depositQueue]
}

export function consumeDeposits(): DepositItem[] {
  return depositQueue.splice(0)
}
