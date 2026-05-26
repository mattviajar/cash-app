export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'

export async function POST(request: Request) {
  void request
  return NextResponse.json(
    { error: 'This feature has been removed.' },
    { status: 403 },
  )
}
