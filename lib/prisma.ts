import { PrismaClient } from '@prisma/client'
import { PrismaPg } from '@prisma/adapter-pg'
import pg from 'pg'

const globalForPrisma = globalThis as unknown as { prisma: PrismaClient }

function createPrismaClient() {
  // Use DIRECT_URL (port 5432, session mode) — pg driver needs prepared statements
  // which are disabled in PgBouncer transaction mode (port 6543 / DATABASE_URL).
  const connectionString = (process.env.DIRECT_URL ?? process.env.DATABASE_URL)!
  const pool = new pg.Pool({ connectionString })
  const adapter = new PrismaPg(pool)
  return new PrismaClient({ adapter })
}

export const prisma = globalForPrisma.prisma ?? createPrismaClient()

if (process.env.NODE_ENV !== 'production') globalForPrisma.prisma = prisma
