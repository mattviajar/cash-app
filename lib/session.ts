import crypto from 'crypto'

export type SessionRole = 'kid' | 'parent'

export type SessionPayload = {
  username: string
  role: SessionRole
  exp: number
}

export const SESSION_COOKIE_NAME = 'cash_session'
export const SHORT_SESSION_MAX_AGE_SECONDS = 60 * 60 * 24
export const LONG_SESSION_MAX_AGE_SECONDS = 60 * 60 * 24 * 7

function getSessionSecret(): string {
  return process.env.SESSION_SECRET ?? 'dev-only-change-me-session-secret'
}

function base64UrlEncode(raw: Buffer | string): string {
  const source = Buffer.isBuffer(raw) ? raw : Buffer.from(raw, 'utf8')
  return source
    .toString('base64')
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/g, '')
}

function base64UrlDecode(raw: string): string {
  const padded = raw.replace(/-/g, '+').replace(/_/g, '/') + '==='.slice((raw.length + 3) % 4)
  return Buffer.from(padded, 'base64').toString('utf8')
}

function signPayload(payloadB64: string): string {
  const signature = crypto.createHmac('sha256', getSessionSecret()).update(payloadB64).digest()
  return base64UrlEncode(signature)
}

export function createSessionToken(username: string, role: SessionRole, maxAgeSeconds: number): string {
  const exp = Math.floor(Date.now() / 1000) + Math.max(60, maxAgeSeconds)
  const payload: SessionPayload = { username, role, exp }
  const payloadB64 = base64UrlEncode(JSON.stringify(payload))
  const signatureB64 = signPayload(payloadB64)
  return `${payloadB64}.${signatureB64}`
}

export function verifySessionToken(token: string): SessionPayload | null {
  if (!token || typeof token !== 'string') {
    return null
  }

  const parts = token.split('.')
  if (parts.length !== 2) {
    return null
  }

  const [payloadB64, signatureB64] = parts
  if (!payloadB64 || !signatureB64) {
    return null
  }

  const expectedSignature = signPayload(payloadB64)
  const expectedBuffer = Buffer.from(expectedSignature)
  const actualBuffer = Buffer.from(signatureB64)

  if (expectedBuffer.length !== actualBuffer.length || !crypto.timingSafeEqual(expectedBuffer, actualBuffer)) {
    return null
  }

  try {
    const parsed = JSON.parse(base64UrlDecode(payloadB64)) as Partial<SessionPayload>
    if (!parsed || typeof parsed.username !== 'string' || (parsed.role !== 'kid' && parsed.role !== 'parent')) {
      return null
    }
    if (typeof parsed.exp !== 'number' || parsed.exp <= Math.floor(Date.now() / 1000)) {
      return null
    }
    return {
      username: parsed.username,
      role: parsed.role,
      exp: parsed.exp,
    }
  } catch {
    return null
  }
}

export function getSessionFromCookieHeader(cookieHeader: string | null): SessionPayload | null {
  if (!cookieHeader) {
    return null
  }

  const cookies = cookieHeader.split(';')
  for (const item of cookies) {
    const [name, ...rest] = item.trim().split('=')
    if (name === SESSION_COOKIE_NAME) {
      const value = rest.join('=')
      return verifySessionToken(value)
    }
  }

  return null
}

export function getSessionFromRequest(request: Request): SessionPayload | null {
  return getSessionFromCookieHeader(request.headers.get('cookie'))
}
