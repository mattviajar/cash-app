import { NextResponse } from 'next/server'

type DeviceStatus = {
  loggedIn: boolean
  name: string
  balance: number
  updatedAt: string
  withdrawActive: boolean
  withdrawState: 'idle' | 'dispensing' | 'complete'
  withdrawAmount: number
  lastWithdrawStartedAt: string
  lastWithdrawCompletedAt: string
}

const defaultStatus: DeviceStatus = {
  loggedIn: false,
  name: '',
  balance: 0,
  updatedAt: new Date(0).toISOString(),
  withdrawActive: false,
  withdrawState: 'idle',
  withdrawAmount: 0,
  lastWithdrawStartedAt: new Date(0).toISOString(),
  lastWithdrawCompletedAt: new Date(0).toISOString(),
}

let currentStatus: DeviceStatus = defaultStatus

export async function GET() {
  return NextResponse.json(currentStatus, {
    headers: {
      'Cache-Control': 'no-store, max-age=0',
    },
  })
}

export async function POST(request: Request) {
  let body: unknown
  try {
    body = await request.json()
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 })
  }

  const payload = body as Record<string, unknown>
  const loggedIn = payload.loggedIn === true
  const name = typeof payload.name === 'string' ? payload.name.trim() : ''
  const balance = Number(payload.balance)
  const withdrawActive = payload.withdrawActive === true
  const withdrawState = payload.withdrawState === 'dispensing' || payload.withdrawState === 'complete'
    ? payload.withdrawState
    : 'idle'
  const withdrawAmount = Number(payload.withdrawAmount)
  const lastWithdrawStartedAt = typeof payload.lastWithdrawStartedAt === 'string'
    ? payload.lastWithdrawStartedAt
    : currentStatus.lastWithdrawStartedAt
  const lastWithdrawCompletedAt = typeof payload.lastWithdrawCompletedAt === 'string'
    ? payload.lastWithdrawCompletedAt
    : currentStatus.lastWithdrawCompletedAt

  currentStatus = {
    loggedIn: loggedIn && name.length > 0,
    name: loggedIn ? name : '',
    balance: Number.isFinite(balance) ? Math.round(balance * 100) / 100 : 0,
    updatedAt: new Date().toISOString(),
    withdrawActive,
    withdrawState,
    withdrawAmount: Number.isFinite(withdrawAmount) ? Math.max(0, Math.round(withdrawAmount * 100) / 100) : currentStatus.withdrawAmount,
    lastWithdrawStartedAt,
    lastWithdrawCompletedAt,
  }

  return NextResponse.json({ ok: true, status: currentStatus })
}