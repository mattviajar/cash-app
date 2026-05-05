import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

export async function POST() {
  const result = await prisma.depositQueue.deleteMany({})
  return NextResponse.json({ ok: true, cleared: result.count })
}
