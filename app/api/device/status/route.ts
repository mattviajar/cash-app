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

export async function GET(request: Request) {
  // Backward compatibility: allow simple query-driven updates from firmware
  // clients that send GET /api/device/status?status=complete.
  // Normal dashboard polling calls this endpoint without query params.
  try {
    const url = new URL(request.url)
    const status = url.searchParams.get('status')
    if (status === 'idle' || status === 'dispensing' || status === 'complete') {
      const amountRaw = Number(url.searchParams.get('amount') ?? currentStatus.withdrawAmount)
      const nowIso = new Date().toISOString()
      currentStatus = {
        ...currentStatus,
        updatedAt: nowIso,
        withdrawState: status,
        withdrawActive: status === 'dispensing',
        withdrawAmount: Number.isFinite(amountRaw)
          ? Math.max(0, Math.round(amountRaw * 100) / 100)
          : currentStatus.withdrawAmount,
        lastWithdrawCompletedAt:
          status === 'complete' ? nowIso : currentStatus.lastWithdrawCompletedAt,
      }
    }
  } catch {
    // Ignore compatibility parsing failures and just return current status.
  }

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