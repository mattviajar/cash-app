import { NextResponse } from 'next/server'
import { consumeDeposits } from '../queue'

export async function POST() {
  const cleared = consumeDeposits().length
  return NextResponse.json({ ok: true, cleared })
}
