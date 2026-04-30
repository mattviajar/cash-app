'use client'

import { useEffect } from 'react'
import { useRouter } from 'next/navigation'

const TRANSACTION_KEYS = [
  'cash_withdrawal_history',
  'cash_pending_withdrawals',
]

export default function ResetTransactionsPage() {
  const router = useRouter()

  useEffect(() => {
    try {
      TRANSACTION_KEYS.forEach((key) => localStorage.removeItem(key))
    } catch {
      // Ignore storage failures and continue navigation.
    }

    // Best-effort server queue clear so old hardware events do not replay.
    void fetch('/api/deposit/clear', { method: 'POST' }).catch(() => undefined)

    router.replace('/dashboard')
  }, [router])

  return (
    <div className="min-h-screen flex items-center justify-center bg-blue-50 px-4">
      <p className="text-center text-lg font-semibold text-blue-700">Clearing transactions...</p>
    </div>
  )
}
