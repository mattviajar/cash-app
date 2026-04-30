import { NextResponse } from 'next/server'

type DeviceStatus = {
  loggedIn: boolean
  name: string
  balance: number
  updatedAt: string
}

const defaultStatus: DeviceStatus = {
  loggedIn: false,
  name: '',
  balance: 0,
  updatedAt: new Date(0).toISOString(),
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

  currentStatus = {
    loggedIn: loggedIn && name.length > 0,
    name: loggedIn ? name : '',
    balance: Number.isFinite(balance) ? Math.round(balance * 100) / 100 : 0,
    updatedAt: new Date().toISOString(),
  }

  return NextResponse.json({ ok: true, status: currentStatus })
}