export const dynamic = 'force-dynamic'

import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

const DEFAULT_TTL_SECONDS = 120

function nowPlusSeconds(seconds: number): Date {
  return new Date(Date.now() + seconds * 1000)
}

export async function GET() {
  const lock = await prisma.deviceLock.upsert({
    where: { id: 1 },
    create: { id: 1 },
    update: {},
  })

  const active = !!lock.holder && !!lock.expiresAt && lock.expiresAt.getTime() > Date.now()
  return NextResponse.json({
    active,
    holder: active ? lock.holder : null,
    mode: active ? lock.mode : null,
    expiresAt: active ? lock.expiresAt : null,
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
  const action = typeof payload.action === 'string' ? payload.action.trim().toLowerCase() : ''
  const username = typeof payload.username === 'string' ? payload.username.trim().toLowerCase() : ''
  const mode = typeof payload.mode === 'string' ? payload.mode.trim().toLowerCase() : 'general'
  const ttlSecondsRaw = Number(payload.ttlSeconds)
  const ttlSeconds = Number.isFinite(ttlSecondsRaw) && ttlSecondsRaw > 0
    ? Math.min(600, Math.floor(ttlSecondsRaw))
    : DEFAULT_TTL_SECONDS

  if (!username) {
    return NextResponse.json({ error: 'Missing username' }, { status: 400 })
  }

  const result = await prisma.$transaction(async (tx) => {
    const lock = await tx.deviceLock.upsert({
      where: { id: 1 },
      create: { id: 1 },
      update: {},
    })

    const isActive = !!lock.holder && !!lock.expiresAt && lock.expiresAt.getTime() > Date.now()

    if (action === 'acquire') {
      if (isActive && lock.holder !== username) {
        return {
          ok: false,
          conflict: true,
          holder: lock.holder,
          mode: lock.mode,
          expiresAt: lock.expiresAt,
        }
      }

      const updated = await tx.deviceLock.update({
        where: { id: 1 },
        data: {
          holder: username,
          mode,
          acquiredAt: new Date(),
          expiresAt: nowPlusSeconds(ttlSeconds),
        },
      })

      return { ok: true, lock: updated }
    }

    if (action === 'heartbeat') {
      if (!isActive || lock.holder !== username) {
        return { ok: false, error: 'Lock not held by user' }
      }
      const updated = await tx.deviceLock.update({
        where: { id: 1 },
        data: { expiresAt: nowPlusSeconds(ttlSeconds) },
      })
      return { ok: true, lock: updated }
    }

    if (action === 'release') {
      if (lock.holder && lock.holder !== username) {
        return {
          ok: false,
          conflict: true,
          holder: lock.holder,
          mode: lock.mode,
          expiresAt: lock.expiresAt,
        }
      }

      const updated = await tx.deviceLock.update({
        where: { id: 1 },
        data: {
          holder: null,
          mode: null,
          acquiredAt: null,
          expiresAt: null,
        },
      })
      return { ok: true, lock: updated }
    }

    return { ok: false, error: 'Unknown action' }
  })

  if (!result.ok && (result as { conflict?: boolean }).conflict) {
    return NextResponse.json(result, { status: 409 })
  }
  if (!result.ok) {
    return NextResponse.json(result, { status: 400 })
  }

  return NextResponse.json(result)
}
