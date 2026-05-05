import { NextResponse } from 'next/server'

// In-memory queue of serial commands to forward to the ESP32.
const commandQueue: string[] = []

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

  commandQueue.push(cmd)
  return NextResponse.json({ ok: true })
}

// Consumer: serial bridge polls this to drain and send commands to ESP32.
export async function GET(request: Request) {
  const { searchParams } = new URL(request.url)
  const consume = searchParams.get('consume') === 'true'

  if (consume) {
    const commands = commandQueue.splice(0)
    return NextResponse.json({ commands })
  }

  return NextResponse.json({ commands: [...commandQueue] })
}
