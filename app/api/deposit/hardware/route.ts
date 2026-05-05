import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

const BUTTON_AMOUNTS: Record<string, number> = {
  add10: 10,
  subtract10: -10,
}

export async function POST(request: Request) {
  let body: unknown
  try {
    body = await request.json()
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 })
  }

  const payload = body as Record<string, unknown>
  const button = typeof payload.button === 'string' ? payload.button.trim() : ''
  const amount = BUTTON_AMOUNTS[button]

  if (!amount) {
    return NextResponse.json(
      { error: 'Unknown button. Use button="add10" or button="subtract10".' },
      { status: 400 }
    )
  }

  const item = await prisma.depositQueue.create({
    data: { amount: Math.round(amount * 100) / 100 },
  })
  return NextResponse.json({ ok: true, item })
}
