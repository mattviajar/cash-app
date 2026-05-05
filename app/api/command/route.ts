export const dynamic = 'force-dynamic'
import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

// Producer: dashboard posts commands here when a withdrawal is approved.
export async function POST(request: Request) {
  let body: unknown
  try {
    body = await request.json()
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 })
  }

  const cmd = String((body as Record<string, unknown>).command ?? '').trim()
  if (!cmd || cmd.length > 128) {
    return NextResponse.json({ error: 'Missing or invalid command' }, { status: 400 })
  }

  await prisma.commandQueue.create({ data: { command: cmd } })
  return NextResponse.json({ ok: true })
}

// Consumer: serial bridge polls this to drain and send commands to ESP32.
export async function GET(request: Request) {
  const { searchParams } = new URL(request.url)
  const consume = searchParams.get('consume') === 'true'

  if (consume) {
    const rows = await prisma.commandQueue.findMany({ orderBy: { id: 'asc' } })
    await prisma.commandQueue.deleteMany({})
    return NextResponse.json({ commands: rows.map((c) => c.command) })
  }

  const rows = await prisma.commandQueue.findMany({ orderBy: { id: 'asc' } })
  return NextResponse.json({ commands: rows.map((c) => c.command) })
}
