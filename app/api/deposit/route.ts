import { NextResponse } from 'next/server'
import { consumeDeposits, enqueueDeposit, snapshotDeposits } from './queue'

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

  const item = enqueueDeposit(amount)
  return NextResponse.json({ ok: true, item })
}

// Consumer endpoint: dashboard polls and drains queue with ?consume=true.
// Without consume flag, returns a read-only snapshot.
export async function GET(request: Request) {
  const { searchParams } = new URL(request.url)
  const consume = searchParams.get('consume') === 'true'

  if (!consume) {
    return NextResponse.json({ deposits: snapshotDeposits() })
  }

  const deposits = consumeDeposits()
  return NextResponse.json({ deposits })
}
