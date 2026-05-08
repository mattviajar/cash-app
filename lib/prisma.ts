import { PrismaClient } from '@prisma/client'
import { PrismaPg } from '@prisma/adapter-pg'

const globalForPrisma = globalThis as unknown as { prisma: PrismaClient }

function createPrismaClient() {
  // Strip pgbouncer=true — transaction mode disables prepared statements
  // which pg driver requires. Port 5432 on Supabase pooler = session mode.
  const rawUrl = process.env.DATABASE_URL!
  const connectionString = rawUrl.replace('?pgbouncer=true', '').replace('&pgbouncer=true', '')
  const adapter = new PrismaPg({ connectionString })
  return new PrismaClient({ adapter })
}

export const prisma = globalForPrisma.prisma ?? createPrismaClient()

if (process.env.NODE_ENV !== 'production') globalForPrisma.prisma = prisma
