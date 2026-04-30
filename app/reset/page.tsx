'use client'

import { useEffect } from 'react'
import { useRouter } from 'next/navigation'

export default function ResetPage() {
  const router = useRouter()

  useEffect(() => {
    try {
      localStorage.clear()
      sessionStorage.clear()
    } catch {
      // Ignore storage clearing failures and still navigate away.
    }

    router.replace('/create-account')
  }, [router])

  return (
    <div className="min-h-screen flex items-center justify-center bg-blue-50 px-4">
      <p className="text-center text-lg font-semibold text-blue-700">Resetting app data...</p>
    </div>
  )
}