export const dynamic = 'force-dynamic'
import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

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

  try {
    const item = await prisma.depositQueue.create({
      data: { amount: Math.round(amount * 100) / 100 },
    })
    return NextResponse.json({ ok: true, item })
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e)
    return NextResponse.json({ error: 'DB error', detail: msg }, { status: 500 })
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
    const msg = e instanceof Error ? e.message : String(e)
    return NextResponse.json({ error: 'DB error', detail: msg }, { status: 500 })
  }
}
