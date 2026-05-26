import type { Prisma, PrismaClient } from '@prisma/client'

type DbClient = PrismaClient | Prisma.TransactionClient

type ParentKidRow = {
  parent_username: string
  kid_username: string
}

async function ensureParentKidLinkTable(db: DbClient) {
  await db.$executeRawUnsafe(`
    CREATE TABLE IF NOT EXISTS "ParentKidLink" (
      parent_username TEXT NOT NULL,
      kid_username TEXT NOT NULL UNIQUE,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      PRIMARY KEY (parent_username, kid_username)
    )
  `)
}

export function getParentUsernameFromRequest(request: Request): string {
  const fromHeader = (request.headers.get('x-parent-username') ?? '').trim().toLowerCase()
  if (fromHeader) return fromHeader

  const { searchParams } = new URL(request.url)
  return (searchParams.get('parent') ?? '').trim().toLowerCase()
}

export async function getOwnedKidUsernames(db: DbClient, parentUsername: string): Promise<string[]> {
  const parent = parentUsername.trim().toLowerCase()
  if (!parent) return []

  await ensureParentKidLinkTable(db)
  const rows = await db.$queryRawUnsafe<ParentKidRow[]>(
    'SELECT kid_username FROM "ParentKidLink" WHERE parent_username = $1 ORDER BY kid_username ASC',
    parent
  )
  return rows.map((row) => row.kid_username)
}

export async function isKidOwnedByParent(db: DbClient, parentUsername: string, kidUsername: string): Promise<boolean> {
  const parent = parentUsername.trim().toLowerCase()
  const kid = kidUsername.trim().toLowerCase()
  if (!parent || !kid) return false

  await ensureParentKidLinkTable(db)
  const rows = await db.$queryRawUnsafe<ParentKidRow[]>(
    'SELECT parent_username, kid_username FROM "ParentKidLink" WHERE parent_username = $1 AND kid_username = $2 LIMIT 1',
    parent,
    kid
  )
  return rows.length > 0
}

export async function linkKidToParent(db: DbClient, parentUsername: string, kidUsername: string): Promise<void> {
  const parent = parentUsername.trim().toLowerCase()
  const kid = kidUsername.trim().toLowerCase()
  if (!parent || !kid) {
    throw new Error('INVALID_PARENT_OR_KID')
  }

  await ensureParentKidLinkTable(db)

  const ownerRows = await db.$queryRawUnsafe<ParentKidRow[]>(
    'SELECT parent_username, kid_username FROM "ParentKidLink" WHERE kid_username = $1 LIMIT 1',
    kid
  )

  if (ownerRows.length > 0 && ownerRows[0].parent_username !== parent) {
    throw new Error('KID_ALREADY_OWNED_BY_ANOTHER_PARENT')
  }

  await db.$executeRawUnsafe(
    'INSERT INTO "ParentKidLink" (parent_username, kid_username) VALUES ($1, $2) ON CONFLICT (kid_username) DO NOTHING',
    parent,
    kid
  )
}

export async function unlinkKidFromParent(db: DbClient, parentUsername: string, kidUsername: string): Promise<void> {
  const parent = parentUsername.trim().toLowerCase()
  const kid = kidUsername.trim().toLowerCase()
  if (!parent || !kid) return

  await ensureParentKidLinkTable(db)
  await db.$executeRawUnsafe(
    'DELETE FROM "ParentKidLink" WHERE parent_username = $1 AND kid_username = $2',
    parent,
    kid
  )
}

export async function unlinkKidFromAnyParent(db: DbClient, kidUsername: string): Promise<void> {
  const kid = kidUsername.trim().toLowerCase()
  if (!kid) return

  await ensureParentKidLinkTable(db)
  await db.$executeRawUnsafe('DELETE FROM "ParentKidLink" WHERE kid_username = $1', kid)
}
