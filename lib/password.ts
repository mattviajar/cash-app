import { randomBytes, scryptSync, timingSafeEqual } from 'crypto'

const SCRYPT_KEY_LEN = 64

export function hashPassword(password: string): string {
  const salt = randomBytes(16)
  const derived = scryptSync(password, salt, SCRYPT_KEY_LEN)
  return `${salt.toString('hex')}:${derived.toString('hex')}`
}

export function verifyPassword(password: string, stored: string): boolean {
  const [saltHex, hashHex] = stored.split(':')
  if (!saltHex || !hashHex) {
    return false
  }

  const salt = Buffer.from(saltHex, 'hex')
  const expected = Buffer.from(hashHex, 'hex')
  const actual = Buffer.from(scryptSync(password, salt, expected.length))

  if (actual.length !== expected.length) {
    return false
  }

  return timingSafeEqual(actual, expected)
}
