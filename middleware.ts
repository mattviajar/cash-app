import { NextRequest, NextResponse } from 'next/server'

const SESSION_COOKIE_NAME = 'cash_session'

function normalizeBase64(base64url: string): string {
  const b64 = base64url.replace(/-/g, '+').replace(/_/g, '/')
  return b64 + '='.repeat((4 - (b64.length % 4)) % 4)
}

function decodeBase64UrlString(base64url: string): string | null {
  try {
    return atob(normalizeBase64(base64url))
  } catch {
    return null
  }
}

function utf8ToBinary(input: string): string {
  return decodeURIComponent(encodeURIComponent(input))
}

function base64UrlFromBytes(bytes: Uint8Array): string {
  let binary = ''
  for (const byte of bytes) {
    binary += String.fromCharCode(byte)
  }
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '')
}

function safeJsonParse(raw: string): { username?: unknown; role?: unknown; exp?: unknown } | null {
  try {
    return JSON.parse(raw) as { username?: unknown; role?: unknown; exp?: unknown }
  } catch {
    return null
  }
}

async function verifySessionToken(token: string): Promise<boolean> {
  const parts = token.split('.')
  if (parts.length !== 2) {
    return false
  }

  const [payloadB64, signatureB64] = parts
  if (!payloadB64 || !signatureB64) {
    return false
  }

  const secret = process.env.SESSION_SECRET ?? 'dev-only-change-me-session-secret'
  const key = await crypto.subtle.importKey(
    'raw',
    new TextEncoder().encode(secret),
    { name: 'HMAC', hash: 'SHA-256' },
    false,
    ['sign']
  )
  const signature = await crypto.subtle.sign('HMAC', key, new TextEncoder().encode(payloadB64))
  const expectedSignature = base64UrlFromBytes(new Uint8Array(signature))

  if (expectedSignature !== signatureB64) {
    return false
  }

  const payloadJsonBinary = decodeBase64UrlString(payloadB64)
  if (!payloadJsonBinary) {
    return false
  }

  const payload = safeJsonParse(payloadJsonBinary)
  if (!payload) {
    return false
  }

  const usernameOk = typeof payload.username === 'string' && payload.username.trim().length > 0
  const roleOk = payload.role === 'kid' || payload.role === 'parent'
  const expOk = typeof payload.exp === 'number' && payload.exp > Math.floor(Date.now() / 1000)

  return usernameOk && roleOk && expOk
}

export async function middleware(request: NextRequest) {
  const token = request.cookies.get(SESSION_COOKIE_NAME)?.value

  if (!token || !(await verifySessionToken(token))) {
    const redirectUrl = new URL('/login', request.url)
    redirectUrl.searchParams.set('next', request.nextUrl.pathname)
    const response = NextResponse.redirect(redirectUrl)
    response.cookies.set({
      name: SESSION_COOKIE_NAME,
      value: '',
      httpOnly: true,
      secure: process.env.NODE_ENV === 'production',
      sameSite: 'lax',
      path: '/',
      maxAge: 0,
    })
    return response
  }

  return NextResponse.next()
}

export const config = {
  matcher: ['/dashboard/:path*'],
}
