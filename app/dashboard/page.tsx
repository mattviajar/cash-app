'use client'

import { useEffect, useMemo, useRef, useState } from 'react'
import { useRouter } from 'next/navigation'

type Role = 'kid' | 'parent'
type MenuKey = 'dashboard' | 'progress' | 'transactions' | 'settings' | 'profile'
type KidQuickSection = 'none' | 'withdraw' | 'activity'
type ParentQuickSection = 'none' | 'deposit' | 'withdraw' | 'activity'
type HistoryKind = 'withdrawal' | 'hardware'

type WithdrawalRecord = {
  id: number
  child: string
  amount: number
  note: string
  when: string
  kind?: HistoryKind
}

type PendingWithdrawal = {
  id: number
  child: string
  amount: number
  note: string
  denomination?: WithdrawDenominationKey | null
  quantity?: number | null
  createdAt: string
}

type WithdrawPhase = 'idle' | 'locking' | 'sending' | 'dispensing' | 'finalizing' | 'done' | 'error'

type WithdrawProgressState = {
  open: boolean
  actor: 'kid' | 'parent'
  amount: number
  etaRemaining: number
  phase: WithdrawPhase
  message: string
}

type Goal = {
  id: number
  name: string
  saved: number
  target: number
}

type MachineInventory = {
  bill20: number
  bill50: number
  bill100: number
  bill500: number
  bill1000: number
  coin1: number
  coin5: number
  coin10: number
  coin20: number
}

type InventoryResponse = {
  inventory?: MachineInventory
  totalValue?: number
}

type DeviceLockStatus = {
  active: boolean
  holder: string | null
  mode: string | null
  expiresAt: string | null
}

type DepositDebugState = {
  kidSince: number
  parentSince: number
  kidEventSince: number
  parentEventSince: number
  lastBatchCount: number
  lastBatchMaxId: number
  lastEventBatchCount: number
  lastEventMaxId: number
  lastBatchAmount: number
  lastPollAt: string
}

type ProfileState = {
  username: string
  email: string
  password: string
  securityAnswer: string
}

const withdrawDenominations = [
  { field: 'bill1000', label: '1000 bill', value: 1000 },
  { field: 'bill500', label: '500 bill', value: 500 },
  { field: 'bill100', label: '100 bill', value: 100 },
  { field: 'bill50', label: '50 bill', value: 50 },
  { field: 'bill20', label: '20 bill', value: 20 },
  { field: 'coin20', label: '20 coin', value: 20 },
  { field: 'coin10', label: '10 coin', value: 10 },
  { field: 'coin5', label: '5 coin', value: 5 },
  { field: 'coin1', label: '1 coin', value: 1 },
] as const

type WithdrawDenominationKey = typeof withdrawDenominations[number]['field']

const menuItems: Array<{ key: MenuKey; label: string; icon: string }> = [
  { key: 'dashboard', label: 'Dashboard', icon: '🏠' },
  { key: 'progress', label: 'Progress', icon: '🎯' },
  { key: 'transactions', label: 'Transactions', icon: '🧾' },
  { key: 'settings', label: 'Settings', icon: '⚙' },
  { key: 'profile', label: 'Profile', icon: '👤' },
]

const initialKidGoals: Goal[] = []

const characterOptions = [
  { id: 'astronaut', emoji: '🧑‍🚀', title: 'Space Saver' },
  { id: 'wizard', emoji: '🧙', title: 'Money Wizard' },
  { id: 'ninja', emoji: '🥷', title: 'Budget Ninja' },
  { id: 'robot', emoji: '🤖', title: 'Smart Saver Bot' },
  { id: 'athlete', emoji: '🏃', title: 'Goal Sprinter' },
  { id: 'artist', emoji: '🧑‍🎨', title: 'Creative Planner' },
]

const STORAGE_KEYS = {
  role: 'cash_role',
  balance: 'cash_kid_balance',
  instant: 'cash_instant_withdrawals',
  pending: 'cash_pending_withdrawals',
  approvedPendingIds: 'cash_approved_pending_withdrawal_ids',
  history: 'cash_withdrawal_history',
  kidNotifications: 'cash_kid_notifications',
  kidShowBalance: 'cash_kid_show_balance',
  kidRequireNote: 'cash_kid_require_note',
  parentAlerts: 'cash_parent_spending_alerts',
  parentAutoApproveLimit: 'cash_parent_auto_approve_limit',
  kidGoals: 'cash_kid_goals',
  kidGoalsByAccount: 'cash_kid_goals_by_account',
  kidCharacter: 'cash_kid_character',
  kidName: 'cash_kid_name',
  validKidAccounts: 'cash_valid_kid_accounts',
  kidBalances: 'cash_kid_balances',
}

const defaultHistory: WithdrawalRecord[] = []
const DEPOSIT_COUNTDOWN_SECONDS = 60

function withComputedGoalSavings(goals: Goal[], availableBalance: number): Goal[] {
  let remaining = Math.max(0, Number.isFinite(availableBalance) ? availableBalance : 0)

  return goals.map((goal) => {
    const target = Math.max(0, Number.isFinite(goal.target) ? goal.target : 0)
    const saved = Math.min(remaining, target)
    remaining = Math.max(0, remaining - saved)

    return {
      ...goal,
      saved: Math.round(saved * 100) / 100,
    }
  })
}

function safeParse<T>(raw: string | null, fallback: T): T {
  if (!raw) return fallback
  try {
    return JSON.parse(raw) as T
  } catch {
    return fallback
  }
}

const PHP_CURRENCY = new Intl.NumberFormat('en-PH', {
  style: 'currency',
  currency: 'PHP',
  minimumFractionDigits: 2,
  maximumFractionDigits: 2,
})

function formatPHP(value: number): string {
  return PHP_CURRENCY.format(Number.isFinite(value) ? value : 0)
}

function getSignedTransactionAmount(item: WithdrawalRecord): number {
  if (item.kind === 'hardware') {
    return item.amount
  }
  return -Math.abs(item.amount)
}

function formatSignedPHP(value: number): string {
  const sign = value >= 0 ? '+' : '-'
  return `${sign}${formatPHP(Math.abs(value))}`
}

function estimateWithdrawalSeconds(amount: number): number {
  const normalized = Math.max(20, Math.round(amount / 20) * 20)
  const billCountEstimate = Math.max(1, Math.ceil(normalized / 20))
  return Math.min(120, Math.max(8, 3 + billCountEstimate * 2))
}

async function persistAccountDeposit(
  username: string,
  amount: number,
  role: Role,
  note: string,
  parentUsername?: string
): Promise<{ balance: number | null }> {
  let lastError = 'Failed to save deposit'

  for (let attempt = 1; attempt <= 3; attempt += 1) {
    try {
      const res = await fetch('/api/accounts/deposit', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          username,
          amount,
          role,
          note,
          source: 'dashboard',
          autoCreate: true,
          parentUsername,
        }),
      })

      if (res.ok) {
        const data = await res.json() as { balance?: number }
        return { balance: typeof data.balance === 'number' ? data.balance : null }
      }

      const data = await res.json().catch(() => ({ error: 'Failed to save deposit' })) as { error?: string }
      lastError = data.error ?? 'Failed to save deposit'
    } catch {
      lastError = 'Network error while saving deposit'
    }

    if (attempt < 3) {
      await new Promise((resolve) => setTimeout(resolve, 250 * attempt))
    }
  }

  throw new Error(lastError)
}

async function fetchAccountBalance(username: string): Promise<number | null> {
  const res = await fetch(`/api/accounts/balance?username=${encodeURIComponent(username)}`, {
    cache: 'no-store',
  })
  if (!res.ok) {
    return null
  }
  const data = await res.json() as { account?: { balance?: number } }
  if (typeof data.account?.balance !== 'number') {
    return null
  }
  return data.account.balance
}

async function fetchDeviceStatus(): Promise<{ withdrawActive?: boolean; withdrawState?: string; updatedAt?: string }> {
  const res = await fetch('/api/device/status', { cache: 'no-store' })
  if (!res.ok) {
    return {}
  }
  return res.json() as Promise<{ withdrawActive?: boolean; withdrawState?: string; updatedAt?: string }>
}

async function fetchDeviceLockStatus(): Promise<DeviceLockStatus> {
  const res = await fetch('/api/device/lock', { cache: 'no-store' })
  if (!res.ok) {
    return { active: false, holder: null, mode: null, expiresAt: null }
  }
  return res.json() as Promise<DeviceLockStatus>
}

async function fetchLatestDepositId(): Promise<number> {
  const res = await fetch('/api/deposit', { cache: 'no-store' })
  if (!res.ok) {
    return -1
  }
  const data = await res.json() as { deposits?: Array<{ id: number }> }
  const ids = (data.deposits ?? []).map((item) => item.id).filter((id) => Number.isFinite(id) && id > 0)
  return ids.length > 0 ? Math.max(...ids) : 0
}

async function acquireDeviceLock(username: string, mode: 'deposit' | 'withdraw'): Promise<{ ok: boolean; message?: string }> {
  const res = await fetch('/api/device/lock', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action: 'acquire', username, mode, ttlSeconds: 120 }),
  })

  if (res.ok) {
    return { ok: true }
  }

  const data = await res.json().catch(() => ({})) as { holder?: string; error?: string }
  if (res.status === 409 && data.holder) {
    return { ok: false, message: `Device is currently in use by ${data.holder}.` }
  }
  return { ok: false, message: data.error ?? 'Unable to lock device right now.' }
}

async function releaseDeviceLock(username: string) {
  await fetch('/api/device/lock', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action: 'release', username }),
  })
}

async function heartbeatDeviceLock(username: string) {
  await fetch('/api/device/lock', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action: 'heartbeat', username, ttlSeconds: 120 }),
  })
}

export default function DashboardPage() {
  const router = useRouter()
  const [role, setRole] = useState<Role>('kid')
  const [activeMenu, setActiveMenu] = useState<MenuKey>('dashboard')
  const [kidQuickSection, setKidQuickSection] = useState<KidQuickSection>('none')
  const [parentQuickSection, setParentQuickSection] = useState<ParentQuickSection>('none')
  const [showKidMoreActions, setShowKidMoreActions] = useState(false)
  const [showParentMoreActions, setShowParentMoreActions] = useState(false)
  const [balance, setBalance] = useState(0)
  const [withdrawNote, setWithdrawNote] = useState('')
  const [withdrawDenomination, setWithdrawDenomination] = useState<WithdrawDenominationKey>('bill100')
  const [withdrawQuantity, setWithdrawQuantity] = useState('1')
  const [kidWithdrawBusy, setKidWithdrawBusy] = useState(false)
  const [machineInventory, setMachineInventory] = useState<MachineInventory | null>(null)
  const [machineInventoryTotalValue, setMachineInventoryTotalValue] = useState(0)
  const [inventoryLoading, setInventoryLoading] = useState(true)
  const [instantWithdrawals, setInstantWithdrawals] = useState(false)
  const [pendingWithdrawals, setPendingWithdrawals] = useState<PendingWithdrawal[]>([])
  const [history, setHistory] = useState<WithdrawalRecord[]>(defaultHistory)
  const [kidGoalsByAccount, setKidGoalsByAccount] = useState<Record<string, Goal[]>>({})
  const [kidNotifications, setKidNotifications] = useState(true)
  const [kidShowBalance, setKidShowBalance] = useState(true)
  const [kidRequireNote, setKidRequireNote] = useState(false)
  const [parentSpendingAlerts, setParentSpendingAlerts] = useState(true)
  const [parentAutoApproveLimit, setParentAutoApproveLimit] = useState(0)
  // Device WiFi configuration (sent to ESP32 via SETWIFI command queue)
  const [wifiSsid, setWifiSsid] = useState('')
  const [wifiPass, setWifiPass] = useState('')
  const [wifiSaving, setWifiSaving] = useState(false)
  const [wifiMessage, setWifiMessage] = useState<{ kind: 'ok' | 'err'; text: string } | null>(null)
  const [newGoalName, setNewGoalName] = useState('')
  const [newGoalTarget, setNewGoalTarget] = useState('')
  const [newParentGoalName, setNewParentGoalName] = useState('')
  const [newParentGoalTarget, setNewParentGoalTarget] = useState('')
  const [kidCharacter, setKidCharacter] = useState('astronaut')
  const [kidName, setKidName] = useState('')
  const [parentName, setParentName] = useState('')
  const [parentBalance, setParentBalance] = useState(0)
  const [profileState, setProfileState] = useState<ProfileState>({
    username: '',
    email: '',
    password: '',
    securityAnswer: '',
  })
  const [profileSaving, setProfileSaving] = useState(false)
  const [profileMessage, setProfileMessage] = useState<{ kind: 'ok' | 'err'; text: string } | null>(null)
  const [validKidAccounts, setValidKidAccounts] = useState<string[]>([])
  const [kidBalances, setKidBalances] = useState<{ [key: string]: number }>({})
  const [newKidUsername, setNewKidUsername] = useState('')
  const [newKidPassword, setNewKidPassword] = useState('')
  const [newKidSecurityQuestion, setNewKidSecurityQuestion] = useState("What's your favorite pet?")
  const [newKidSecurityAnswer, setNewKidSecurityAnswer] = useState('')
  const [newKidCustomQuestion, setNewKidCustomQuestion] = useState('')
  const [withdrawProgress, setWithdrawProgress] = useState<WithdrawProgressState>({
    open: false,
    actor: 'kid',
    amount: 0,
    etaRemaining: 0,
    phase: 'idle',
    message: '',
  })
  const [activeWithdrawLockOwner, setActiveWithdrawLockOwner] = useState<string | null>(null)
  const [deviceLockStatus, setDeviceLockStatus] = useState<DeviceLockStatus>({
    active: false,
    holder: null,
    mode: null,
    expiresAt: null,
  })
  const [parentWithdrawNote, setParentWithdrawNote] = useState('')
  const [parentWithdrawBusy, setParentWithdrawBusy] = useState(false)
  const [parentChildWithdrawKid, setParentChildWithdrawKid] = useState('')
  const [parentChildWithdrawNote, setParentChildWithdrawNote] = useState('')
  const [parentChildWithdrawBusy, setParentChildWithdrawBusy] = useState(false)
  const [pendingDepositKid, setPendingDepositKid] = useState<string | null>(null)
  const [pendingDepositTarget, setPendingDepositTarget] = useState(0)
  const [pendingDepositReceived, setPendingDepositReceived] = useState(0)
  const [pendingDepositError, setPendingDepositError] = useState<string | null>(null)
  const [isHydrated, setIsHydrated] = useState(false)
  const [depositToast, setDepositToast] = useState<string | null>(null)
  const [lastHardwareDepositAt, setLastHardwareDepositAt] = useState<string | null>(null)
  const [lastHardwareDepositAmount, setLastHardwareDepositAmount] = useState<number | null>(null)
  const [depositDebug, setDepositDebug] = useState<DepositDebugState>({
    kidSince: 0,
    parentSince: 0,
    kidEventSince: 0,
    parentEventSince: 0,
    lastBatchCount: 0,
    lastBatchMaxId: 0,
    lastEventBatchCount: 0,
    lastEventMaxId: 0,
    lastBatchAmount: 0,
    lastPollAt: '',
  })
  const [kidDepositModalOpen, setKidDepositModalOpen] = useState(false)
  const [depositCountdown, setDepositCountdown] = useState(DEPOSIT_COUNTDOWN_SECONDS)
  const kidWithdrawInFlightRef = useRef(false)
  const kidLastSeenDepositIdRef = useRef(0)
  const kidLastSeenDepositEventIdRef = useRef(0)
  const parentLastSeenDepositIdRef = useRef(0)
  const parentLastSeenDepositEventIdRef = useRef(0)
  const withdrawStartedAtRef = useRef(0)

  const handleLogout = async () => {
    try {
      await fetch('/api/auth/logout', { method: 'POST' })
    } catch {
      // Continue local sign-out flow even if request fails.
    }
    sessionStorage.clear()
    router.replace('/login')
  }

  const refreshInventory = async () => {
    try {
      const res = await fetch('/api/inventory', { cache: 'no-store' })
      if (!res.ok) {
        return
      }
      const data = await res.json() as InventoryResponse
      setMachineInventory(data.inventory ?? null)
      setMachineInventoryTotalValue(Math.round(Number(data.totalValue ?? 0) * 100) / 100)
    } catch {
      // Keep current inventory view if polling momentarily fails.
    }
  }

  const kidGoalInputs = useMemo(() => {
    if (!kidName) return initialKidGoals
    return kidGoalsByAccount[kidName] ?? initialKidGoals
  }, [kidGoalsByAccount, kidName])

  const kidGoals = useMemo(() => {
    return withComputedGoalSavings(kidGoalInputs, balance)
  }, [kidGoalInputs, balance])

  const parentGoalInputs = useMemo(() => {
    if (!parentName) return initialKidGoals
    return kidGoalsByAccount[parentName] ?? initialKidGoals
  }, [kidGoalsByAccount, parentName])

  const parentOwnGoals = useMemo(() => {
    return withComputedGoalSavings(parentGoalInputs, parentBalance)
  }, [parentGoalInputs, parentBalance])

  useEffect(() => {
    const hydrateSession = async () => {
      const sessionRes = await fetch('/api/auth/session', { cache: 'no-store' })
      if (!sessionRes.ok) {
        sessionStorage.clear()
        router.replace('/login')
        return
      }

      const sessionData = await sessionRes.json() as {
        account: { username: string; role: Role; balance: number }
      }
      const role = sessionData.account.role
      const username = sessionData.account.username

      sessionStorage.setItem(STORAGE_KEYS.role, role)
      sessionStorage.setItem('cash_username', username)
      setRole(role)

      const loadedGoalsByAccount = safeParse<Record<string, Goal[]>>(
        localStorage.getItem(STORAGE_KEYS.kidGoalsByAccount),
        {}
      )

      // If kid, set their name from username
      if (role === 'kid') {
        if (username) {
          setKidName(username)

          if (!loadedGoalsByAccount[username]) {
            const legacyGoals = safeParse<Goal[]>(
              localStorage.getItem(STORAGE_KEYS.kidGoals),
              initialKidGoals
            )
            if (legacyGoals.length > 0) {
              loadedGoalsByAccount[username] = legacyGoals
            }
          }
        }
      } else {
        if (username) {
          setParentName(username)
          setParentBalance(Math.round((sessionData.account.balance ?? 0) * 100) / 100)
        }
      }

      setKidGoalsByAccount(loadedGoalsByAccount)

      setInstantWithdrawals(localStorage.getItem(STORAGE_KEYS.instant) === 'true')

      const storedNotifications = localStorage.getItem(STORAGE_KEYS.kidNotifications)
      if (storedNotifications) {
        setKidNotifications(storedNotifications === 'true')
      }

      const storedShowBalance = localStorage.getItem(STORAGE_KEYS.kidShowBalance)
      if (storedShowBalance) {
        setKidShowBalance(storedShowBalance === 'true')
      }

      const storedRequireNote = localStorage.getItem(STORAGE_KEYS.kidRequireNote)
      if (storedRequireNote) {
        setKidRequireNote(storedRequireNote === 'true')
      }

      const storedParentAlerts = localStorage.getItem(STORAGE_KEYS.parentAlerts)
      if (storedParentAlerts) {
        setParentSpendingAlerts(storedParentAlerts === 'true')
      }

      const storedAutoApproveLimit = localStorage.getItem(STORAGE_KEYS.parentAutoApproveLimit)
      if (storedAutoApproveLimit) {
        const parsedLimit = Number(storedAutoApproveLimit)
        if (Number.isFinite(parsedLimit) && parsedLimit >= 0) {
          setParentAutoApproveLimit(parsedLimit)
        }
      }

      const storedCharacter = localStorage.getItem(STORAGE_KEYS.kidCharacter)
      if (storedCharacter) {
        setKidCharacter(storedCharacter)
      }

      try {
        const parentScope = role === 'parent' && username ? `?parent=${encodeURIComponent(username)}` : ''
        const kidsUrl = role === 'parent'
          ? `/api/auth/kids${parentScope}`
          : `/api/auth/kids${username ? `?username=${encodeURIComponent(username)}` : ''}`
        const pendingUrl = role === 'parent'
          ? `/api/pending-withdrawals${parentScope}`
          : `/api/pending-withdrawals${username ? `?child=${encodeURIComponent(username)}` : ''}`
        const txUrl = role === 'parent'
          ? `/api/accounts/transactions${parentScope}`
          : `/api/accounts/transactions${username ? `?username=${encodeURIComponent(username)}` : ''}`

        const [kidsRes, pendingRes, txRes] = await Promise.all([
          fetch(kidsUrl, { cache: 'no-store' }),
          fetch(pendingUrl, { cache: 'no-store' }),
          fetch(txUrl, { cache: 'no-store' }),
        ])

        if (kidsRes.ok) {
          const kidsData = await kidsRes.json() as { kids: Array<{ username: string; balance: number }> }
          const names = kidsData.kids.map((kid) => kid.username)
          const balances = kidsData.kids.reduce<Record<string, number>>((acc, kid) => {
            acc[kid.username] = kid.balance
            return acc
          }, {})
          setValidKidAccounts(names)
          setKidBalances(balances)
          if (role === 'kid' && username) {
            setBalance(balances[username] ?? 0)
          }
        }

        if (pendingRes.ok) {
          const pendingData = await pendingRes.json() as { pending: PendingWithdrawal[] }
          setPendingWithdrawals(pendingData.pending)
        }

        if (txRes.ok) {
          const txData = await txRes.json() as {
            transactions: Array<{
              id: number
              child: string
              amount: number
              signedAmount?: number
              note: string
              when: string
              kind: string
            }>
          }
          setHistory(
            txData.transactions.map((entry) => ({
              id: entry.id,
              child: entry.child,
              amount: Math.abs(entry.signedAmount ?? entry.amount),
              note: entry.note,
              when: entry.when,
              kind: (entry.kind.includes('deposit') ? 'hardware' : 'withdrawal') as HistoryKind,
            }))
          )
        }
      } catch {
        // Keep UI usable even when API is temporarily unavailable.
      }

      setIsHydrated(true)
    }

    void hydrateSession()

  }, [router])

  useEffect(() => {
    if (!isHydrated) return

    const username = role === 'kid' ? kidName : parentName
    if (!username) return

    let cancelled = false

    const loadProfile = async () => {
      try {
        const res = await fetch('/api/auth/profile', { cache: 'no-store' })
        if (!res.ok) {
          return
        }

        const data = await res.json() as {
          account?: {
            username?: string
            email?: string
            securityAnswer?: string
          }
        }

        if (cancelled || !data.account) return

        setProfileState({
          username: data.account.username ?? username,
          email: data.account.email ?? '',
          password: '',
          securityAnswer: data.account.securityAnswer ?? '',
        })
      } catch {
        // Keep the editor usable with whatever local values already exist.
      }
    }

    void loadProfile()

    return () => {
      cancelled = true
    }
  }, [isHydrated, role, kidName, parentName])

  useEffect(() => {
    if (!isHydrated) return
    localStorage.setItem(STORAGE_KEYS.instant, String(instantWithdrawals))
    localStorage.setItem(STORAGE_KEYS.kidNotifications, String(kidNotifications))
    localStorage.setItem(STORAGE_KEYS.kidShowBalance, String(kidShowBalance))
    localStorage.setItem(STORAGE_KEYS.kidRequireNote, String(kidRequireNote))
    localStorage.setItem(STORAGE_KEYS.parentAlerts, String(parentSpendingAlerts))
    localStorage.setItem(STORAGE_KEYS.parentAutoApproveLimit, String(parentAutoApproveLimit))
    localStorage.setItem(STORAGE_KEYS.kidGoalsByAccount, JSON.stringify(kidGoalsByAccount))
    localStorage.setItem(STORAGE_KEYS.kidCharacter, kidCharacter)
  }, [
    isHydrated,
    instantWithdrawals,
    kidNotifications,
    kidShowBalance,
    kidRequireNote,
    parentSpendingAlerts,
    parentAutoApproveLimit,
    kidGoalsByAccount,
    kidCharacter,
  ])

  // NOTE: We intentionally do NOT mirror `balance` back into `kidBalances`
  // here. Every transaction site (deposit/withdraw/initial load) already
  // updates `kidBalances` explicitly. A reciprocal effect would form a
  // feedback loop with the kidBalances→balance sync below and cause the UI
  // to jitter between the cached default and the freshly-fetched DB value.

  useEffect(() => {
    if (!isHydrated || role !== 'parent' || !parentName) return

    let cancelled = false
    const syncParentBalance = async () => {
      const dbBalance = await fetchAccountBalance(parentName)
      if (!cancelled && dbBalance !== null) {
        setParentBalance(Math.round(dbBalance * 100) / 100)
      }
    }

    void syncParentBalance()
    return () => {
      cancelled = true
    }
  }, [isHydrated, role, parentName])

  // Keep key account data synced from server to avoid stale UI during multi-session use.
  useEffect(() => {
    if (!isHydrated) return

    let cancelled = false

    const syncAuthoritativeData = async () => {
      try {
        const sessionRes = await fetch('/api/auth/session', { cache: 'no-store' })
        if (!sessionRes.ok) {
          if (!cancelled) {
            sessionStorage.clear()
            router.replace('/login')
          }
          return
        }

        const sessionData = await sessionRes.json() as {
          account: { username: string; role: Role; balance: number }
        }

        if (role === 'kid' && kidName) {
          const [balanceRes, pendingRes, txRes] = await Promise.all([
            fetch(`/api/accounts/balance?username=${encodeURIComponent(kidName)}`, { cache: 'no-store' }),
            fetch(`/api/pending-withdrawals?child=${encodeURIComponent(kidName)}`, { cache: 'no-store' }),
            fetch(`/api/accounts/transactions?username=${encodeURIComponent(kidName)}`, { cache: 'no-store' }),
          ])

          if (balanceRes.ok) {
            const balanceData = await balanceRes.json() as { account?: { balance?: number } }
            const nextBalance = Number(balanceData.account?.balance ?? 0)
            if (!cancelled && Number.isFinite(nextBalance)) {
              const rounded = Math.round(nextBalance * 100) / 100
              setBalance(rounded)
              setKidBalances((prev) => ({ ...prev, [kidName]: rounded }))
            }
          }

          if (pendingRes.ok) {
            const pendingData = await pendingRes.json() as { pending: PendingWithdrawal[] }
            if (!cancelled) {
              setPendingWithdrawals(pendingData.pending)
            }
          }

          if (txRes.ok) {
            const txData = await txRes.json() as {
              transactions: Array<{
                id: number
                child: string
                amount: number
                signedAmount?: number
                note: string
                when: string
                kind: string
              }>
            }
            if (!cancelled) {
              setHistory(
                txData.transactions.map((entry) => ({
                  id: entry.id,
                  child: entry.child,
                  amount: Math.abs(entry.signedAmount ?? entry.amount),
                  note: entry.note,
                  when: entry.when,
                  kind: (entry.kind.includes('deposit') ? 'hardware' : 'withdrawal') as HistoryKind,
                }))
              )
            }
          }
        }

        if (role === 'parent' && parentName) {
          const [kidsRes, pendingRes, txRes, parentBalanceRes] = await Promise.all([
            fetch(`/api/auth/kids?parent=${encodeURIComponent(parentName)}`, { cache: 'no-store' }),
            fetch(`/api/pending-withdrawals?parent=${encodeURIComponent(parentName)}`, { cache: 'no-store' }),
            fetch(`/api/accounts/transactions?parent=${encodeURIComponent(parentName)}`, { cache: 'no-store' }),
            fetch(`/api/accounts/balance?username=${encodeURIComponent(parentName)}`, { cache: 'no-store' }),
          ])

          if (kidsRes.ok) {
            const kidsData = await kidsRes.json() as { kids: Array<{ username: string; balance: number }> }
            if (!cancelled) {
              setValidKidAccounts(kidsData.kids.map((kid) => kid.username))
              setKidBalances(
                kidsData.kids.reduce<Record<string, number>>((acc, kid) => {
                  acc[kid.username] = kid.balance
                  return acc
                }, {})
              )
            }
          }

          if (pendingRes.ok) {
            const pendingData = await pendingRes.json() as { pending: PendingWithdrawal[] }
            if (!cancelled) {
              setPendingWithdrawals(pendingData.pending)
            }
          }

          if (txRes.ok) {
            const txData = await txRes.json() as {
              transactions: Array<{
                id: number
                child: string
                amount: number
                signedAmount?: number
                note: string
                when: string
                kind: string
              }>
            }
            if (!cancelled) {
              setHistory(
                txData.transactions.map((entry) => ({
                  id: entry.id,
                  child: entry.child,
                  amount: Math.abs(entry.signedAmount ?? entry.amount),
                  note: entry.note,
                  when: entry.when,
                  kind: (entry.kind.includes('deposit') ? 'hardware' : 'withdrawal') as HistoryKind,
                }))
              )
            }
          }

          if (parentBalanceRes.ok) {
            const balanceData = await parentBalanceRes.json() as { account?: { balance?: number } }
            const nextBalance = Number(balanceData.account?.balance ?? sessionData.account.balance ?? 0)
            if (!cancelled && Number.isFinite(nextBalance)) {
              setParentBalance(Math.round(nextBalance * 100) / 100)
            }
          }
        }
      } catch {
        // Ignore temporary sync errors; next cycle will retry.
      }
    }

    void syncAuthoritativeData()
    const interval = setInterval(() => { void syncAuthoritativeData() }, 2000)

    return () => {
      cancelled = true
      clearInterval(interval)
    }
  }, [isHydrated, role, kidName, parentName, router])

  // Keep kid UI balance aligned with the authoritative per-account balance map.
  useEffect(() => {
    if (!isHydrated) return
    if (role !== 'kid' || !kidName) return
    const mapped = kidBalances[kidName] ?? 0
    setBalance((prev) => (prev === mapped ? prev : mapped))
  }, [isHydrated, role, kidName, kidBalances])

  useEffect(() => {
    if (role !== 'parent') return
    if (validKidAccounts.length === 0) {
      setParentChildWithdrawKid('')
      return
    }

    setParentChildWithdrawKid((current) => {
      if (current && validKidAccounts.includes(current)) {
        return current
      }
      return validKidAccounts[0]
    })
  }, [role, validKidAccounts])

  useEffect(() => {
    if (role === 'kid' && activeMenu === 'settings') {
      setActiveMenu('dashboard')
    }
  }, [role, activeMenu])

  useEffect(() => {
    if (!isHydrated) return

    const controller = new AbortController()
    const publishDeviceStatus = async () => {
      try {
        await fetch('/api/device/status', {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
          },
          body: JSON.stringify({
            loggedIn: role === 'kid' && kidName.length > 0,
            name: role === 'kid' ? kidName : '',
            balance: role === 'kid' ? balance : 0,
          }),
          signal: controller.signal,
        })
      } catch {
        // Ignore sync failures so the dashboard keeps working offline.
      }
    }

    void publishDeviceStatus()
    return () => controller.abort()
  }, [isHydrated, role, kidName, balance])

  useEffect(() => {
    if (!isHydrated) return

    let cancelled = false
    const loadInventory = async () => {
      setInventoryLoading(true)
      try {
        if (!cancelled) {
          await refreshInventory()
        }
      } catch {
        if (!cancelled) {
          setMachineInventory(null)
          setMachineInventoryTotalValue(0)
        }
      } finally {
        if (!cancelled) {
          setInventoryLoading(false)
        }
      }
    }

    void loadInventory()
    return () => {
      cancelled = true
    }
  }, [isHydrated])

  useEffect(() => {
    if (!isHydrated) return

    let cancelled = false
    const tick = async () => {
      if (cancelled) return
      await refreshInventory()
    }

    const interval = setInterval(() => { void tick() }, 2000)
    return () => {
      cancelled = true
      clearInterval(interval)
    }
  }, [isHydrated])

  useEffect(() => {
    if (!isHydrated) return

    let cancelled = false
    const pollLock = async () => {
      const status = await fetchDeviceLockStatus()
      if (!cancelled) {
        setDeviceLockStatus(status)
      }
    }

    void pollLock()
    const interval = setInterval(() => { void pollLock() }, 1000)

    return () => {
      cancelled = true
      clearInterval(interval)
    }
  }, [isHydrated])

  useEffect(() => {
    if (!isHydrated) return

    const depositActive = kidDepositModalOpen || pendingDepositKid !== null
    const withdrawActive = withdrawProgress.open && (
      withdrawProgress.phase === 'locking' ||
      withdrawProgress.phase === 'sending' ||
      withdrawProgress.phase === 'dispensing' ||
      withdrawProgress.phase === 'finalizing'
    )

    if (!depositActive && !withdrawActive) {
      return
    }

    let cancelled = false
    const pollInventory = async () => {
      if (cancelled) return
      await refreshInventory()
    }

    void pollInventory()
    const interval = setInterval(() => { void pollInventory() }, 1000)

    return () => {
      cancelled = true
      clearInterval(interval)
    }
  }, [
    isHydrated,
    kidDepositModalOpen,
    pendingDepositKid,
    withdrawProgress.open,
    withdrawProgress.phase,
  ])

  useEffect(() => {
    if (!isHydrated) return

    const depositActive = kidDepositModalOpen || pendingDepositKid !== null
    const withdrawActive = withdrawProgress.open && (
      withdrawProgress.phase === 'locking' ||
      withdrawProgress.phase === 'sending' ||
      withdrawProgress.phase === 'dispensing' ||
      withdrawProgress.phase === 'finalizing'
    )

    const lockOwner = activeWithdrawLockOwner
      ?? (kidDepositModalOpen ? kidName : (pendingDepositKid ? (parentName || pendingDepositKid) : null))

    if (!lockOwner || (!depositActive && !withdrawActive)) {
      return
    }

    let cancelled = false
    const sendHeartbeat = async () => {
      if (cancelled) return
      await heartbeatDeviceLock(lockOwner)
    }

    void sendHeartbeat()
    const interval = setInterval(() => { void sendHeartbeat() }, 30000)

    return () => {
      cancelled = true
      clearInterval(interval)
    }
  }, [
    isHydrated,
    activeWithdrawLockOwner,
    kidDepositModalOpen,
    pendingDepositKid,
    parentName,
    kidName,
    withdrawProgress.open,
    withdrawProgress.phase,
  ])

  // Poll for hardware deposits only while the kid deposit modal is open.
  useEffect(() => {
    if (!isHydrated || role !== 'kid' || !kidName || !kidDepositModalOpen) return

    let cancelled = false
    let running = false
    const pollDeposits = async () => {
      if (running) return
      running = true
      try {
        if (kidLastSeenDepositIdRef.current > 0) {
          const latestId = await fetchLatestDepositId()
          if (latestId >= 0 && kidLastSeenDepositIdRef.current > latestId) {
            kidLastSeenDepositIdRef.current = latestId
          }
        }
        const since = kidLastSeenDepositIdRef.current
        const eventSince = kidLastSeenDepositEventIdRef.current
        setDepositDebug((prev) => ({ ...prev, kidSince: since, kidEventSince: eventSince }))
        const res = await fetch(`/api/deposit?since=${since}&eventSince=${eventSince}`, { cache: 'no-store' })
        if (!res.ok) { running = false; return }
        const data = await res.json() as {
          deposits: { id: number; amount: number }[]
          events?: { id: number; amount: number }[]
        }
        const deposits = data.deposits ?? []
        const events = data.events ?? []
        if (deposits.length === 0 && events.length === 0) { running = false; return }

        const maxId = deposits.length > 0 ? Math.max(...deposits.map((d) => d.id)) : 0
        const maxEventId = events.length > 0 ? Math.max(...events.map((e) => e.id)) : 0
        const depositBatchAmount = Math.round(deposits.filter((d) => d.amount > 0).reduce((sum, d) => sum + d.amount, 0) * 100) / 100
        const eventBatchAmount = Math.round(events.filter((e) => e.amount > 0).reduce((sum, e) => sum + e.amount, 0) * 100) / 100
        const batchAmount = deposits.length > 0 ? depositBatchAmount : eventBatchAmount

        setDepositDebug((prev) => ({
          ...prev,
          lastBatchCount: deposits.length,
          lastBatchMaxId: maxId,
          lastEventBatchCount: events.length,
          lastEventMaxId: maxEventId,
          lastBatchAmount: batchAmount,
          lastPollAt: new Date().toLocaleTimeString('en-PH'),
        }))
        if (deposits.length > 0) {
          kidLastSeenDepositIdRef.current = Math.max(kidLastSeenDepositIdRef.current, ...deposits.map((d) => d.id))
        }
        if (events.length > 0) {
          kidLastSeenDepositEventIdRef.current = Math.max(kidLastSeenDepositEventIdRef.current, ...events.map((e) => e.id))
        }

        const total = batchAmount
        if (total > 0 && !cancelled) {
          setPendingDepositReceived((prev) => Math.round((prev + total) * 100) / 100)
          setLastHardwareDepositAt(new Date().toLocaleTimeString('en-PH'))
          setLastHardwareDepositAmount(total)
          setDepositCountdown(DEPOSIT_COUNTDOWN_SECONDS)
        }
      } catch {
        // Silently ignore network errors
      }
      running = false
    }

    void pollDeposits()
    const interval = setInterval(() => { void pollDeposits() }, 350)

    return () => {
      cancelled = true
      clearInterval(interval)
    }
  }, [isHydrated, role, kidName, kidDepositModalOpen])

  // Parent-supervised deposit mode: accumulate cash-in while session is active.
  useEffect(() => {
    if (!isHydrated || role !== 'parent' || !pendingDepositKid) return

    let cancelled = false
    let running = false
    const pollDeposits = async () => {
      if (running) return
      running = true
      try {
        if (parentLastSeenDepositIdRef.current > 0) {
          const latestId = await fetchLatestDepositId()
          if (latestId >= 0 && parentLastSeenDepositIdRef.current > latestId) {
            parentLastSeenDepositIdRef.current = latestId
          }
        }
        const since = parentLastSeenDepositIdRef.current
        const eventSince = parentLastSeenDepositEventIdRef.current
        setDepositDebug((prev) => ({ ...prev, parentSince: since, parentEventSince: eventSince }))
        const res = await fetch(`/api/deposit?since=${since}&eventSince=${eventSince}`, { cache: 'no-store' })
        if (!res.ok) { running = false; return }
        const data = await res.json() as {
          deposits: { id: number; amount: number }[]
          events?: { id: number; amount: number }[]
        }
        const deposits = data.deposits ?? []
        const events = data.events ?? []
        if ((deposits.length === 0 && events.length === 0) || cancelled) { running = false; return }

        const maxId = deposits.length > 0 ? Math.max(...deposits.map((d) => d.id)) : 0
        const maxEventId = events.length > 0 ? Math.max(...events.map((e) => e.id)) : 0
        const depositBatchAmount = Math.round(
          deposits
            .filter((d) => Number.isFinite(d.amount) && d.amount > 0)
            .reduce((sum, d) => sum + d.amount, 0) * 100
        ) / 100
        const eventBatchAmount = Math.round(
          events
            .filter((e) => Number.isFinite(e.amount) && e.amount > 0)
            .reduce((sum, e) => sum + e.amount, 0) * 100
        ) / 100
        const total = deposits.length > 0 ? depositBatchAmount : eventBatchAmount

        setDepositDebug((prev) => ({
          ...prev,
          lastBatchCount: deposits.length,
          lastBatchMaxId: maxId,
          lastEventBatchCount: events.length,
          lastEventMaxId: maxEventId,
          lastBatchAmount: total,
          lastPollAt: new Date().toLocaleTimeString('en-PH'),
        }))
        if (deposits.length > 0) {
          parentLastSeenDepositIdRef.current = Math.max(parentLastSeenDepositIdRef.current, ...deposits.map((d) => d.id))
        }
        if (events.length > 0) {
          parentLastSeenDepositEventIdRef.current = Math.max(parentLastSeenDepositEventIdRef.current, ...events.map((e) => e.id))
        }

        if (total > 0) {
          setPendingDepositReceived((prev) => Math.round((prev + total) * 100) / 100)
          setLastHardwareDepositAt(new Date().toLocaleTimeString('en-PH'))
          setLastHardwareDepositAmount(total)
          setPendingDepositError(null)
          setDepositCountdown(DEPOSIT_COUNTDOWN_SECONDS)
        }
      } catch {
        if (!cancelled) {
          setPendingDepositError('Waiting for hardware bridge...')
        }
      }
      running = false
    }

    void pollDeposits()
    const interval = setInterval(() => { void pollDeposits() }, 300)

    return () => {
      cancelled = true
      clearInterval(interval)
    }
  }, [isHydrated, role, pendingDepositKid])

  // Countdown tick — decrements every second while a deposit session is active.
  useEffect(() => {
    const sessionActive = kidDepositModalOpen || pendingDepositKid !== null
    if (!sessionActive || depositCountdown <= 0) return
    const t = setTimeout(() => setDepositCountdown((c) => c - 1), 1000)
    return () => clearTimeout(t)
  }, [kidDepositModalOpen, pendingDepositKid, depositCountdown])

  // Commit deposit when countdown reaches zero.
  useEffect(() => {
    if (depositCountdown !== 0) return

    void (async () => {
      if (kidDepositModalOpen) {
        if (pendingDepositReceived > 0) {
          const credited = Math.round(pendingDepositReceived * 100) / 100
          try {
            const saved = await persistAccountDeposit(kidName, credited, 'kid', 'Hardware deposit')
            const latestBalance = saved.balance ?? await fetchAccountBalance(kidName)
            if (latestBalance !== null) {
              setBalance(latestBalance)
              setKidBalances((prev) => ({ ...prev, [kidName]: latestBalance }))
            } else {
              setBalance((prev) => Math.round((prev + credited) * 100) / 100)
            }

            setHistory((prev) => [
              {
                id: Date.now(),
                child: kidName,
                amount: credited,
                note: 'Hardware deposit',
                when: 'Just now',
                kind: 'hardware',
              },
              ...prev,
            ])
            setDepositToast(`+${formatPHP(credited)} deposited!`)
            setTimeout(() => setDepositToast(null), 4000)
            setPendingDepositError(null)
          } catch (error) {
            const message = error instanceof Error ? error.message : 'Failed to save deposit'
            setPendingDepositError(`Deposit detected but not saved yet: ${message}`)
            setDepositCountdown(10)
            return
          }
        }
        setPendingDepositReceived(0)
        setKidDepositModalOpen(false)
        if (kidName) {
          void releaseDeviceLock(kidName)
        }
      } else if (pendingDepositKid) {
        if (pendingDepositReceived > 0) {
          const credited = Math.round(pendingDepositReceived * 100) / 100
          try {
            if (pendingDepositKid === parentName) {
              const saved = await persistAccountDeposit(parentName, credited, 'parent', 'Hardware deposit (self)', parentName)
              const latestBalance = saved.balance ?? await fetchAccountBalance(parentName)
              if (latestBalance !== null) {
                setParentBalance(latestBalance)
              } else {
                setParentBalance((prev) => Math.round((prev + credited) * 100) / 100)
              }
            } else {
              const saved = await persistAccountDeposit(pendingDepositKid, credited, 'kid', 'Hardware deposit (parent confirmation)', parentName)
              const latestBalance = saved.balance ?? await fetchAccountBalance(pendingDepositKid)
              if (latestBalance !== null) {
                setKidBalances((prev) => ({ ...prev, [pendingDepositKid]: latestBalance }))
              } else {
                setKidBalances((prev) => ({
                  ...prev,
                  [pendingDepositKid]: Math.round(((prev[pendingDepositKid] ?? 0) + credited) * 100) / 100,
                }))
              }
            }

            setHistory((prev) => [
              {
                id: Date.now(),
                child: pendingDepositKid,
                amount: credited,
                note: 'Hardware deposit (parent confirmation)',
                when: 'Just now',
                kind: 'hardware',
              },
              ...prev,
            ])
            setDepositToast(`${formatPHP(credited)} deposited to ${pendingDepositKid}`)
            setTimeout(() => setDepositToast(null), 4000)
            setPendingDepositError(null)
          } catch (error) {
            const message = error instanceof Error ? error.message : 'Failed to save deposit'
            setPendingDepositError(`Deposit detected but not saved yet: ${message}`)
            setDepositCountdown(10)
            return
          }
        }
        const locker = parentName || pendingDepositKid
        if (locker) {
          void releaseDeviceLock(locker)
        }
        setPendingDepositKid(null)
        setPendingDepositReceived(0)
        setPendingDepositError(null)
      }
    })()
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [depositCountdown])

  const visibleMenuItems = role === 'kid' ? menuItems.filter((item) => item.key !== 'settings') : menuItems

  const kidHistory = history.filter((entry) => entry.child === kidName)
  const pendingForKid = pendingWithdrawals.filter((entry) => entry.child === kidName)
  const kidHistorySigned = kidHistory.map((item) => ({
    ...item,
    signedAmount: getSignedTransactionAmount(item),
  }))
  const totalGoalSaved = kidGoals.reduce((sum, goal) => sum + goal.saved, 0)
  const totalGoalTarget = kidGoals.reduce((sum, goal) => sum + goal.target, 0)
  const totalGoalProgress = totalGoalTarget > 0 ? Math.round((totalGoalSaved / totalGoalTarget) * 100) : 0
  const topGoal = (kidGoals.length > 0 ? [...kidGoals].sort((a, b) => b.saved / b.target - a.saved / a.target)[0] : null)
  const outgoingKidTransactions = kidHistorySigned.filter((item) => item.signedAmount < 0)
  const totalKidSpent = outgoingKidTransactions.reduce((sum, item) => sum + Math.abs(item.signedAmount), 0)
  const kidAverageWithdrawal = outgoingKidTransactions.length > 0 ? totalKidSpent / outgoingKidTransactions.length : 0
  const kidLargestWithdrawal = outgoingKidTransactions.length > 0
    ? Math.max(...outgoingKidTransactions.map((item) => Math.abs(item.signedAmount)))
    : 0
  const kidApprovalRate = kidHistory.length + pendingForKid.length > 0
    ? Math.round((kidHistory.length / (kidHistory.length + pendingForKid.length)) * 100)
    : 100
  const selectedCharacter = characterOptions.find((option) => option.id === kidCharacter) ?? characterOptions[0]
  
  const kidSpendingByNote = Object.entries(
    kidHistorySigned.reduce<Record<string, number>>((acc, item) => {
      if (item.signedAmount >= 0) {
        return acc
      }
      const key = item.note.trim() || 'Other'
      acc[key] = (acc[key] ?? 0) + Math.abs(item.signedAmount)
      return acc
    }, {})
  ).slice(0, 5)
  const parentPending = pendingWithdrawals
  const parentDepositTargets = [
    ...(parentName ? [parentName] : []),
    ...validKidAccounts,
  ]
  const parentChildren = validKidAccounts.map((username, index) => ({
    id: index + 1,
    name: username,
    balance: kidBalances[username] || 0,
    withdrawalsThisWeek: history.filter((entry) => entry.child === username).length,
  }))
  const selectedWithdrawDenomination = withdrawDenominations.find((option) => option.field === withdrawDenomination) ?? withdrawDenominations[2]
  const selectedWithdrawCount = Math.max(1, Number.isFinite(Number(withdrawQuantity)) ? Math.round(Number(withdrawQuantity)) : 1)
  const selectedWithdrawAmount = selectedWithdrawDenomination.value * selectedWithdrawCount
  const selectedWithdrawInventoryCount = machineInventory?.[selectedWithdrawDenomination.field] ?? 0
  const machineCashStock = withdrawDenominations.map((option) => ({
    field: option.field,
    label: option.label,
    count: machineInventory?.[option.field] ?? 0,
  }))
  const selectedChildBalance = useMemo(() => {
    if (!parentChildWithdrawKid) return 0
    return kidBalances[parentChildWithdrawKid] ?? parentChildren.find((child) => child.name === parentChildWithdrawKid)?.balance ?? 0
  }, [kidBalances, parentChildWithdrawKid, parentChildren])
  const canWithdrawBySelection = selectedWithdrawAmount > 0 && selectedWithdrawInventoryCount >= selectedWithdrawCount
  const canParentChildWithdraw = !!parentChildWithdrawKid && canWithdrawBySelection && selectedWithdrawAmount <= selectedChildBalance
  const parentSpendingByChild = parentChildren.map((child) => ({
    name: child.name,
    amount: history
      .filter((entry) => entry.child === child.name)
      .reduce((sum, entry) => {
        const signed = getSignedTransactionAmount(entry)
        return signed < 0 ? sum + Math.abs(signed) : sum
      }, 0),
  }))
  const outgoingHistory = history.filter((entry) => getSignedTransactionAmount(entry) < 0)
  const totalOutgoingHistoryAmount = outgoingHistory.reduce(
    (sum, entry) => sum + Math.abs(getSignedTransactionAmount(entry)),
    0
  )
  const parentGoals = validKidAccounts.flatMap((username) => {
    const rawGoals = kidGoalsByAccount[username] ?? []
    const computedGoals = withComputedGoalSavings(rawGoals, kidBalances[username] ?? 0)

    return computedGoals.map((goal) => ({
      ...goal,
      child: username,
      key: `${username}-${goal.id}`,
    }))
  })

  const currentUserName = (role === 'kid' ? kidName : parentName).trim().toLowerCase()
  const lockHeldByOtherUser =
    deviceLockStatus.active &&
    !!deviceLockStatus.holder &&
    !!currentUserName &&
    deviceLockStatus.holder !== currentUserName
  const deviceUsableForCurrentUser = !lockHeldByOtherUser

  const canWithdraw = useMemo(() => {
    const noteOk = !kidRequireNote || withdrawNote.trim().length >= 3
    return (
      selectedWithdrawAmount > 0 &&
      selectedWithdrawAmount <= balance &&
      noteOk &&
      canWithdrawBySelection &&
      !inventoryLoading
    )
  }, [balance, canWithdrawBySelection, inventoryLoading, kidRequireNote, selectedWithdrawAmount, withdrawNote])

  const canParentWithdraw = selectedWithdrawAmount > 0 && selectedWithdrawAmount <= parentBalance && canWithdrawBySelection && !inventoryLoading

  const parentAlerts = [
    instantWithdrawals
      ? 'Instant withdrawals are ON for kids.'
      : `Pending approvals: ${parentPending.length}`,
    parentSpendingAlerts
      ? 'Spending alerts are enabled.'
      : 'Spending alerts are disabled.',
    topGoal && topGoal.saved / topGoal.target > 0.7
      ? `${kidName} is close to a savings goal.`
      : 'No new goal alerts today.',
  ]

  const startWithdrawProgress = (actor: 'kid' | 'parent', amount: number) => {
    withdrawStartedAtRef.current = Date.now()
    setWithdrawProgress({
      open: true,
      actor,
      amount,
      etaRemaining: estimateWithdrawalSeconds(amount),
      phase: 'locking',
      message: 'Securing the ATM device lock...',
    })
  }

  const setWithdrawPhase = (phase: WithdrawPhase, message: string) => {
    setWithdrawProgress((prev) => ({ ...prev, open: true, phase, message }))
  }

  const setWithdrawError = (message: string) => {
    setWithdrawProgress((prev) => ({
      ...prev,
      open: true,
      phase: 'error',
      message,
    }))
  }

  const resetWithdrawForm = () => {
    setWithdrawQuantity('1')
    setWithdrawNote('')
  }

  const saveProfile = async () => {
    if (profileSaving) return

    const username = profileState.username.trim().toLowerCase()
    const email = profileState.email.trim().toLowerCase()
    const password = profileState.password
    const securityAnswer = profileState.securityAnswer.trim()

    if (!username) {
      setProfileMessage({ kind: 'err', text: 'Username is required.' })
      return
    }

    if (!securityAnswer) {
      setProfileMessage({ kind: 'err', text: 'Security answer cannot be empty.' })
      return
    }

    setProfileSaving(true)
    setProfileMessage(null)

    try {
      const res = await fetch('/api/auth/profile', {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          username,
          email,
          password,
          securityAnswer,
        }),
      })

      const data = await res.json().catch(() => ({ error: 'Failed to update profile' })) as {
        ok?: boolean
        error?: string
        account?: {
          username?: string
          email?: string
          securityAnswer?: string
          role?: Role
          parentUsername?: string
        }
      }

      if (!res.ok || !data.ok || !data.account) {
        setProfileMessage({ kind: 'err', text: data.error ?? 'Failed to update profile.' })
        return
      }

      const nextUsername = data.account.username ?? username
      const nextEmail = data.account.email ?? email
      const nextSecurityAnswer = data.account.securityAnswer ?? securityAnswer

      setProfileState((prev) => ({
        ...prev,
        username: nextUsername,
        email: nextEmail,
        password: '',
        securityAnswer: nextSecurityAnswer,
      }))

      if ((data.account.role ?? role) === 'kid') {
        setKidName(nextUsername)
      } else {
        setParentName(nextUsername)
      }

      sessionStorage.setItem('cash_username', nextUsername)
      setProfileMessage({ kind: 'ok', text: 'Profile saved successfully.' })
    } catch {
      setProfileMessage({ kind: 'err', text: 'Network error while saving profile.' })
    } finally {
      setProfileSaving(false)
    }
  }

  const handleWithdraw = async (e: React.FormEvent) => {
    e.preventDefault()
    if (!canWithdraw || kidWithdrawInFlightRef.current) return

    kidWithdrawInFlightRef.current = true
    setKidWithdrawBusy(true)

    try {

      const amount = selectedWithdrawAmount
      const note = withdrawNote.trim() || 'Withdrawal'

      if (instantWithdrawals || (parentAutoApproveLimit > 0 && amount <= parentAutoApproveLimit)) {
        startWithdrawProgress('kid', amount)
        const lock = await acquireDeviceLock(kidName, 'withdraw')
        if (!lock.ok) {
          setWithdrawError(lock.message ?? 'Unable to lock device right now.')
          alert(`Error: ${lock.message ?? 'Unable to lock device right now.'}`)
          return
        }
        setActiveWithdrawLockOwner(kidName)

        setWithdrawPhase('sending', 'Sending withdrawal command to the machine...')
        const res = await fetch('/api/command', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            command: `WITHDRAW ${Math.round(amount)}`,
            denomination: selectedWithdrawDenomination.field,
            quantity: selectedWithdrawCount,
            account: kidName,
            lockOwner: kidName,
            role: 'kid',
            autoCreate: true,
            note,
          }),
        })

        if (!res.ok) {
          const data = await res.json().catch(() => ({ error: 'Withdrawal failed' }))
          setWithdrawError(data.error ?? 'Withdrawal failed')
          alert(`Error: ${data.error ?? 'Withdrawal failed'}`)
          return
        }

        setWithdrawPhase('dispensing', 'Dispensing cash now...')

        const updatedBalance = await fetchAccountBalance(kidName)
        if (updatedBalance !== null) {
          setBalance(updatedBalance)
          setKidBalances((prev) => ({ ...prev, [kidName]: updatedBalance }))
        }

        const txRes = await fetch(`/api/accounts/transactions?username=${encodeURIComponent(kidName)}`, { cache: 'no-store' })
        if (txRes.ok) {
          const txData = await txRes.json() as { transactions: Array<{ id: number; child: string; amount: number; signedAmount?: number; note: string; when: string; kind: string }> }
          setHistory(
            txData.transactions.map((entry) => ({
              id: entry.id,
              child: entry.child,
              amount: Math.abs(entry.signedAmount ?? entry.amount),
              note: entry.note,
              when: entry.when,
              kind: (entry.kind.includes('deposit') ? 'hardware' : 'withdrawal') as HistoryKind,
            }))
          )
        }

        void refreshInventory()
      } else {
        const createRes = await fetch('/api/pending-withdrawals', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            child: kidName,
            amount,
            note,
            denomination: selectedWithdrawDenomination.field,
            quantity: selectedWithdrawCount,
          }),
        })
        if (!createRes.ok) {
          const data = await createRes.json().catch(() => ({ error: 'Failed to create withdrawal request' }))
          alert(`Error: ${data.error ?? 'Failed to create withdrawal request'}`)
          return
        }

        const pendingRes = await fetch(`/api/pending-withdrawals?child=${encodeURIComponent(kidName)}`, { cache: 'no-store' })
        if (pendingRes.ok) {
          const pendingData = await pendingRes.json() as { pending: PendingWithdrawal[] }
          setPendingWithdrawals(pendingData.pending)
        }

        alert(`Request sent: ${formatPHP(amount)} withdrawal request has been submitted for parent approval.`)
      }

      resetWithdrawForm()
    } finally {
      kidWithdrawInFlightRef.current = false
      setKidWithdrawBusy(false)
    }
  }

  const handleParentWithdraw = async (e: React.FormEvent) => {
    e.preventDefault()
    if (!parentName || !canParentWithdraw || parentWithdrawBusy) return

    const amount = selectedWithdrawAmount
    const note = parentWithdrawNote.trim() || 'Parent withdrawal'

    setParentWithdrawBusy(true)
    try {
      startWithdrawProgress('parent', amount)
      const lock = await acquireDeviceLock(parentName, 'withdraw')
      if (!lock.ok) {
        setWithdrawError(lock.message ?? 'Unable to lock device right now.')
        alert(`❌ ${lock.message}`)
        return
      }
      setActiveWithdrawLockOwner(parentName)

      setWithdrawPhase('sending', 'Sending parent withdrawal command...')
      const res = await fetch('/api/command', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          command: `WITHDRAW ${Math.round(amount)}`,
          denomination: selectedWithdrawDenomination.field,
          quantity: selectedWithdrawCount,
          account: parentName,
          lockOwner: parentName,
          role: 'parent',
          autoCreate: true,
          note,
        }),
      })

      if (!res.ok) {
        const data = await res.json().catch(() => ({ error: 'Parent withdrawal failed' }))
        setWithdrawError(data.error ?? 'Parent withdrawal failed')
        alert(`❌ ${data.error ?? 'Parent withdrawal failed'}`)
        return
      }

      setWithdrawPhase('dispensing', 'Dispensing parent cash withdrawal...')

      const latestParentBalance = await fetchAccountBalance(parentName)
      if (latestParentBalance !== null) {
        setParentBalance(Math.round(latestParentBalance * 100) / 100)
      }

      const txRes = await fetch(`/api/accounts/transactions?parent=${encodeURIComponent(parentName)}`, { cache: 'no-store' })
      if (txRes.ok) {
        const txData = await txRes.json() as {
          transactions: Array<{
            id: number
            child: string
            amount: number
            signedAmount?: number
            note: string
            when: string
            kind: string
          }>
        }
        setHistory(
          txData.transactions.map((entry) => ({
            id: entry.id,
            child: entry.child,
            amount: Math.abs(entry.signedAmount ?? entry.amount),
            note: entry.note,
            when: entry.when,
            kind: (entry.kind.includes('deposit') ? 'hardware' : 'withdrawal') as HistoryKind,
          }))
        )
      }

      void refreshInventory()

      setParentWithdrawNote('')
      setWithdrawQuantity('1')
    } finally {
      setParentWithdrawBusy(false)
    }
  }

  const handleParentChildWithdraw = async (e: React.FormEvent) => {
    e.preventDefault()
    if (!parentName || !parentChildWithdrawKid || parentChildWithdrawBusy) return

    const amount = selectedWithdrawAmount
    const note = parentChildWithdrawNote.trim() || `Parent withdrew from ${parentChildWithdrawKid}`
    const childBalance = kidBalances[parentChildWithdrawKid] ?? parentChildren.find((child) => child.name === parentChildWithdrawKid)?.balance ?? 0

    if (!Number.isFinite(amount) || amount <= 0 || amount > childBalance) {
      return
    }

    setParentChildWithdrawBusy(true)
    try {
      startWithdrawProgress('parent', amount)
      const lock = await acquireDeviceLock(parentName, 'withdraw')
      if (!lock.ok) {
        setWithdrawError(lock.message ?? 'Unable to lock device right now.')
        alert(`❌ ${lock.message}`)
        return
      }
      setActiveWithdrawLockOwner(parentName)

      setWithdrawPhase('sending', `Withdrawing from ${parentChildWithdrawKid}...`)
      const res = await fetch('/api/command', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          command: `WITHDRAW ${Math.round(amount)}`,
          denomination: selectedWithdrawDenomination.field,
          quantity: selectedWithdrawCount,
          account: parentChildWithdrawKid,
          lockOwner: parentName,
          parentUsername: parentName,
          role: 'kid',
          autoCreate: true,
          note,
        }),
      })

      if (!res.ok) {
        const data = await res.json().catch(() => ({ error: 'Child withdrawal failed' }))
        setWithdrawError(data.error ?? 'Child withdrawal failed')
        alert(`❌ ${data.error ?? 'Child withdrawal failed'}`)
        return
      }

      setWithdrawPhase('dispensing', `Dispensing ${formatPHP(amount)} from ${parentChildWithdrawKid}...`)

      const [kidsRes, txRes] = await Promise.all([
        fetch(`/api/auth/kids?parent=${encodeURIComponent(parentName)}`, { cache: 'no-store' }),
        fetch(`/api/accounts/transactions?parent=${encodeURIComponent(parentName)}`, { cache: 'no-store' }),
      ])

      if (kidsRes.ok) {
        const kidsData = await kidsRes.json() as { kids: Array<{ username: string; balance: number }> }
        const balances = kidsData.kids.reduce<Record<string, number>>((acc, kid) => {
          acc[kid.username] = kid.balance
          return acc
        }, {})
        setKidBalances(balances)
      }

      if (txRes.ok) {
        const txData = await txRes.json() as {
          transactions: Array<{
            id: number
            child: string
            amount: number
            signedAmount?: number
            note: string
            when: string
            kind: string
          }>
        }
        setHistory(
          txData.transactions.map((entry) => ({
            id: entry.id,
            child: entry.child,
            amount: Math.abs(entry.signedAmount ?? entry.amount),
            note: entry.note,
            when: entry.when,
            kind: (entry.kind.includes('deposit') ? 'hardware' : 'withdrawal') as HistoryKind,
          }))
        )
      }

      void refreshInventory()

      setParentChildWithdrawNote('')
      setWithdrawQuantity('1')
    } finally {
      setParentChildWithdrawBusy(false)
    }
  }

  useEffect(() => {
    if (!withdrawProgress.open || withdrawProgress.phase !== 'dispensing') return

    if (withdrawProgress.etaRemaining <= 0) {
      setWithdrawProgress((prev) => ({
        ...prev,
        phase: 'finalizing',
        message: 'Waiting for the machine to confirm the last bill/coin cleared...',
        etaRemaining: 0,
      }))
      return
    }

    const timer = setTimeout(() => {
      setWithdrawProgress((prev) => {
        if (!prev.open || prev.phase !== 'dispensing') return prev
        return {
          ...prev,
          etaRemaining: Math.max(0, prev.etaRemaining - 1),
        }
      })
    }, 1000)

    return () => clearTimeout(timer)
  }, [withdrawProgress])

  useEffect(() => {
    if (!withdrawProgress.open || (withdrawProgress.phase !== 'dispensing' && withdrawProgress.phase !== 'finalizing')) return

    let cancelled = false
    let running = false

    const pollStatus = async () => {
      if (running) return
      running = true
      try {
        const status = await fetchDeviceStatus()
        if (cancelled) return

        const completedAt = status.updatedAt ? Date.parse(status.updatedAt) : 0
        if (status.withdrawState === 'complete' && Number.isFinite(completedAt) && completedAt >= withdrawStartedAtRef.current) {
          setWithdrawProgress((prev) => ({
            ...prev,
            phase: 'done',
            etaRemaining: 0,
            message: 'Withdrawal finished. Please collect your cash.',
          }))
          return
        }

        if (withdrawProgress.phase === 'finalizing') {
          setWithdrawProgress((prev) => {
            if (!prev.open || prev.phase !== 'finalizing') return prev
            return {
              ...prev,
              message: status.withdrawActive
                ? 'Machine is still checking the final bills and coins...'
                : 'Waiting for the machine to confirm completion...',
            }
          })
        }
      } finally {
        running = false
      }
    }

    void pollStatus()
    const interval = setInterval(() => { void pollStatus() }, 1000)

    return () => {
      cancelled = true
      clearInterval(interval)
    }
  }, [withdrawProgress.open, withdrawProgress.phase])

  useEffect(() => {
    if (!withdrawProgress.open || withdrawProgress.phase !== 'done') return

    const timer = setTimeout(() => {
      setWithdrawProgress((prev) => ({ ...prev, open: false, phase: 'idle' }))
    }, 2500)

    return () => clearTimeout(timer)
  }, [withdrawProgress.open, withdrawProgress.phase])

  useEffect(() => {
    if (!activeWithdrawLockOwner) return
    if (withdrawProgress.phase !== 'done' && withdrawProgress.phase !== 'error') return

    const owner = activeWithdrawLockOwner
    setActiveWithdrawLockOwner(null)
    void releaseDeviceLock(owner)
  }, [activeWithdrawLockOwner, withdrawProgress.phase])

  const approvePending = async (id: number) => {
    const request = pendingWithdrawals.find((item) => item.id === id)
    if (!request) return

    const locker = parentName || request.child
    startWithdrawProgress('parent', request.amount)
    const lock = await acquireDeviceLock(locker, 'withdraw')
    if (!lock.ok) {
      setWithdrawError(lock.message ?? 'Unable to lock device right now.')
      alert(`❌ ${lock.message}`)
      return
    }
    setActiveWithdrawLockOwner(locker)

    setWithdrawPhase('sending', `Sending withdrawal for ${request.child}...`)
    const commandRes = await fetch('/api/command', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        command: `WITHDRAW ${Math.round(request.amount)}`,
        denomination: request.denomination,
        quantity: request.quantity,
        account: request.child,
        lockOwner: locker,
        parentUsername: parentName || locker,
        role: 'kid',
        autoCreate: true,
        note: request.note,
      }),
    })

    if (!commandRes.ok) {
      const data = await commandRes.json().catch(() => ({ error: 'Failed to process withdrawal request' }))
      setWithdrawError(data.error ?? 'Failed to process withdrawal request')
      alert(`❌ ${data.error ?? 'Failed to process withdrawal request'}`)
      return
    }

    setWithdrawPhase('dispensing', `Dispensing ${formatPHP(request.amount)} for ${request.child}...`)

    await fetch(`/api/pending-withdrawals?id=${id}&parent=${encodeURIComponent(parentName || locker)}`, { method: 'DELETE' })

    const [kidsRes, pendingRes, txRes] = await Promise.all([
      fetch(`/api/auth/kids?parent=${encodeURIComponent(parentName || locker)}`, { cache: 'no-store' }),
      fetch(`/api/pending-withdrawals?parent=${encodeURIComponent(parentName || locker)}`, { cache: 'no-store' }),
      fetch(`/api/accounts/transactions?parent=${encodeURIComponent(parentName || locker)}`, { cache: 'no-store' }),
    ])

    if (kidsRes.ok) {
      const kidsData = await kidsRes.json() as { kids: Array<{ username: string; balance: number }> }
      const balances = kidsData.kids.reduce<Record<string, number>>((acc, kid) => {
        acc[kid.username] = kid.balance
        return acc
      }, {})
      setKidBalances(balances)
    }
    if (pendingRes.ok) {
      const pendingData = await pendingRes.json() as { pending: PendingWithdrawal[] }
      setPendingWithdrawals(pendingData.pending)
    }
    if (txRes.ok) {
      const txData = await txRes.json() as { transactions: Array<{ id: number; child: string; amount: number; signedAmount?: number; note: string; when: string; kind: string }> }
      setHistory(
        txData.transactions.map((entry) => ({
          id: entry.id,
          child: entry.child,
          amount: Math.abs(entry.signedAmount ?? entry.amount),
          note: entry.note,
          when: entry.when,
          kind: (entry.kind.includes('deposit') ? 'hardware' : 'withdrawal') as HistoryKind,
        }))
      )
    }
  }

  const declinePending = async (id: number) => {
    const parentQuery = parentName ? `&parent=${encodeURIComponent(parentName)}` : ''
    const res = await fetch(`/api/pending-withdrawals?id=${id}${parentQuery}`, { method: 'DELETE' })
    if (!res.ok) {
      alert('❌ Failed to decline request')
      return
    }
    setPendingWithdrawals((prev) => prev.filter((item) => item.id !== id))
  }

  const startParentDepositFlow = async (username: string) => {
    const locker = parentName || username
    const lock = await acquireDeviceLock(locker, 'deposit')
    if (!lock.ok) {
      alert(`❌ ${lock.message}`)
      return
    }

    // Snapshot current max ID so only deposits AFTER this moment are counted.
    // Always reset cursor to a concrete value (including 0) to avoid stale
    // high-water marks blocking future detections.
    parentLastSeenDepositIdRef.current = 0
    parentLastSeenDepositEventIdRef.current = 0
    try {
      const res = await fetch('/api/deposit', { cache: 'no-store' })
      const data = await res.json() as { deposits: { id: number }[]; events?: { id: number }[] }
      const maxId = data.deposits?.length > 0 ? Math.max(...data.deposits.map((d) => d.id)) : 0
      const maxEventId = data.events && data.events.length > 0 ? Math.max(...data.events.map((e) => e.id)) : 0
      parentLastSeenDepositIdRef.current = maxId
      parentLastSeenDepositEventIdRef.current = maxEventId
    } catch {
      parentLastSeenDepositIdRef.current = 0
      parentLastSeenDepositEventIdRef.current = 0
    }

    setPendingDepositKid(username)
    setPendingDepositTarget(0)
    setPendingDepositReceived(0)
    setPendingDepositError(null)
    setDepositCountdown(DEPOSIT_COUNTDOWN_SECONDS)
  }

  const cancelParentDepositFlow = () => {
    const locker = parentName || pendingDepositKid || ''
    if (locker) {
      void releaseDeviceLock(locker)
    }
    parentLastSeenDepositIdRef.current = 0
    parentLastSeenDepositEventIdRef.current = 0
    setPendingDepositKid(null)
    setPendingDepositTarget(0)
    setPendingDepositReceived(0)
    setPendingDepositError(null)
    setDepositCountdown(DEPOSIT_COUNTDOWN_SECONDS)
  }

  const applyParentReceivedDeposit = async () => {
    if (!pendingDepositKid || pendingDepositReceived <= 0) return

    const credited = Math.round(pendingDepositReceived * 100) / 100
    try {
      if (pendingDepositKid === parentName) {
        const saved = await persistAccountDeposit(parentName, credited, 'parent', 'Hardware deposit (manual confirmation)', parentName)
        const latestBalance = saved.balance ?? await fetchAccountBalance(parentName)
        if (latestBalance !== null) {
          setParentBalance(latestBalance)
        } else {
          setParentBalance((prev) => Math.round((prev + credited) * 100) / 100)
        }
      } else {
        const saved = await persistAccountDeposit(pendingDepositKid, credited, 'kid', 'Hardware deposit (manual confirmation)', parentName)
        const latestBalance = saved.balance ?? await fetchAccountBalance(pendingDepositKid)
        if (latestBalance !== null) {
          setKidBalances((prev) => ({ ...prev, [pendingDepositKid]: latestBalance }))
        } else {
          setKidBalances((prev) => ({
            ...prev,
            [pendingDepositKid]: Math.round(((prev[pendingDepositKid] ?? 0) + credited) * 100) / 100,
          }))
        }
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to save deposit'
      setPendingDepositError(`Deposit detected but not saved yet: ${message}`)
      return
    }

    setHistory((prev) => [
      {
        id: Date.now(),
        child: pendingDepositKid,
        amount: credited,
        note: 'Hardware deposit (manual confirmation)',
        when: 'Just now',
        kind: 'hardware',
      },
      ...prev,
    ])
    setPendingDepositError(null)
    setDepositToast(`${formatPHP(credited)} deposited to ${pendingDepositKid}`)
    setTimeout(() => setDepositToast(null), 4000)
    cancelParentDepositFlow()
  }

  const handleAddGoal = (e: React.FormEvent) => {
    e.preventDefault()
    const cleanedName = newGoalName.trim()
    const targetValue = Number(newGoalTarget)
    if (!kidName || !cleanedName || !Number.isFinite(targetValue) || targetValue <= 0) {
      return
    }

    const newGoal: Goal = {
      id: Date.now(),
      name: cleanedName,
      saved: 0,
      target: Number(targetValue.toFixed(2)),
    }

    setKidGoalsByAccount((prev) => ({
      ...prev,
      [kidName]: [...(prev[kidName] ?? []), newGoal],
    }))
    setNewGoalName('')
    setNewGoalTarget('')
  }

  const handleAddParentGoal = (e: React.FormEvent) => {
    e.preventDefault()
    const cleanedName = newParentGoalName.trim()
    const targetValue = Number(newParentGoalTarget)
    if (!parentName || !cleanedName || !Number.isFinite(targetValue) || targetValue <= 0) {
      return
    }

    const newGoal: Goal = {
      id: Date.now(),
      name: cleanedName,
      saved: 0,
      target: Number(targetValue.toFixed(2)),
    }

    setKidGoalsByAccount((prev) => ({
      ...prev,
      [parentName]: [...(prev[parentName] ?? []), newGoal],
    }))
    setNewParentGoalName('')
    setNewParentGoalTarget('')
  }

  const requestGoalWithdrawal = async (goal: Goal) => {
    if (!kidName) return

    const amount = Number(goal.target)
    if (!Number.isFinite(amount) || amount <= 0) {
      return
    }

    if (balance < amount) {
      alert(`Not enough balance for this goal. Required: ${formatPHP(amount)}, Available: ${formatPHP(balance)}`)
      return
    }

    const createRes = await fetch('/api/pending-withdrawals', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        child: kidName,
        amount,
        note: `Goal withdrawal: ${goal.name}`,
      }),
    })

    if (!createRes.ok) {
      const data = await createRes.json().catch(() => ({ error: 'Failed to create withdrawal request' }))
      alert(`Error: ${data.error ?? 'Failed to create withdrawal request'}`)
      return
    }

    const pendingRes = await fetch(`/api/pending-withdrawals?child=${encodeURIComponent(kidName)}`, { cache: 'no-store' })
    if (pendingRes.ok) {
      const pendingData = await pendingRes.json() as { pending: PendingWithdrawal[] }
      setPendingWithdrawals(pendingData.pending)
    }

    alert(`Request sent: ${formatPHP(amount)} for goal "${goal.name}" is pending parent approval.`)
  }

  const openKidDepositModal = async () => {
    const lock = await acquireDeviceLock(kidName, 'deposit')
    if (!lock.ok) {
      alert(`❌ ${lock.message}`)
      return
    }

    kidLastSeenDepositIdRef.current = 0
    kidLastSeenDepositEventIdRef.current = 0
    try {
      const res = await fetch('/api/deposit', { cache: 'no-store' })
      const data = await res.json() as { deposits: { id: number }[]; events?: { id: number }[] }
      kidLastSeenDepositIdRef.current = data.deposits?.length > 0
        ? Math.max(...data.deposits.map((d) => d.id))
        : 0
      kidLastSeenDepositEventIdRef.current = data.events && data.events.length > 0
        ? Math.max(...data.events.map((e) => e.id))
        : 0
    } catch {
      kidLastSeenDepositIdRef.current = 0
      kidLastSeenDepositEventIdRef.current = 0
    }

    setPendingDepositReceived(0)
    setPendingDepositError(null)
    setDepositCountdown(DEPOSIT_COUNTDOWN_SECONDS)
    setKidDepositModalOpen(true)
  }

  const kidDashboardView = (
    <section className="space-y-4 sm:space-y-6">
      <div className="relative overflow-hidden rounded-2xl border-2 border-blue-200 bg-gradient-to-r from-blue-600 via-sky-500 to-teal-500 p-5 sm:p-6 shadow-lg">
        <div className="pointer-events-none absolute inset-0 bg-[radial-gradient(circle_at_12%_20%,rgba(255,255,255,0.24),transparent_34%),linear-gradient(180deg,rgba(255,255,255,0.08),transparent)]" />
        <div className="relative z-10 max-w-sm">
        <p className="text-[11px] uppercase tracking-[0.22em] text-white/80 font-inter font-semibold">Current Balance</p>
        <h2 className="text-4xl sm:text-5xl font-sora font-black text-white mt-2 drop-shadow-[0_6px_18px_rgba(12,44,87,0.34)]">
          {kidShowBalance ? formatPHP(balance) : '•••••'}
        </h2>
        <p className="text-sm text-white/88 font-inter mt-3 leading-relaxed">
          {lastHardwareDepositAt && lastHardwareDepositAmount !== null
            ? `Last received ${formatPHP(lastHardwareDepositAmount)} at ${lastHardwareDepositAt}`
            : 'No recent hardware deposit'}
        </p>
        </div>
      </div>

      <div className="grid grid-cols-2 sm:grid-cols-4 gap-2">
        <button
          type="button"
          onClick={() => { void openKidDepositModal() }}
          disabled={lockHeldByOtherUser}
          className="dashboard-action-primary disabled:opacity-50"
        >
          <span className="sm:hidden text-lg">💰</span>
          <span className="hidden sm:inline">Deposit</span>
        </button>
        <button
          type="button"
          onClick={() => setKidQuickSection((prev) => prev === 'withdraw' ? 'none' : 'withdraw')}
          className="dashboard-action-secondary"
        >
          <span className="sm:hidden text-lg">💸</span>
          <span className="hidden sm:inline">Withdraw</span>
        </button>
        <button
          type="button"
          onClick={() => setShowKidMoreActions((prev) => !prev)}
          className="dashboard-action-soft"
        >
          <span className="sm:hidden text-lg">⋯</span>
          <span className="hidden sm:inline">{showKidMoreActions ? 'Less' : 'More'}</span>
        </button>
      </div>

      {showKidMoreActions && (
      <div className="grid grid-cols-2 sm:grid-cols-3 gap-2">
        <button
          type="button"
          onClick={() => setKidQuickSection((prev) => prev === 'activity' ? 'none' : 'activity')}
          className="dashboard-action-soft"
        >
          <span className="sm:hidden text-lg">🧾</span>
          <span className="hidden sm:inline">Activity</span>
        </button>
        <button
          type="button"
          onClick={() => setActiveMenu('profile')}
          className="dashboard-action-soft"
        >
          <span className="sm:hidden text-lg">👤</span>
          <span className="hidden sm:inline">Profile</span>
        </button>
        <button
          type="button"
          onClick={() => setActiveMenu('settings')}
          className="dashboard-action-soft"
        >
          <span className="sm:hidden text-lg">⚙️</span>
          <span className="hidden sm:inline">Settings</span>
        </button>
      </div>
      )}

      {kidQuickSection === 'withdraw' && (
      <div className="dashboard-panel">
        <h3 className="text-lg sm:text-xl font-sora font-bold text-gray-900 mb-3">Withdraw Cash</h3>
        <form onSubmit={handleWithdraw} className="grid gap-2 sm:gap-4 md:grid-cols-4">
          <select
            value={withdrawDenomination}
            onChange={(e) => setWithdrawDenomination(e.target.value as WithdrawDenominationKey)}
            className="dashboard-field"
          >
            {withdrawDenominations.map((option) => {
              const count = machineInventory?.[option.field] ?? 0
              return (
                <option key={option.field} value={option.field}>
                  {option.label} • {inventoryLoading ? 'loading...' : `${count} in stock`}
                </option>
              )
            })}
          </select>
          <input
            type="number"
            min="1"
            step="1"
            value={withdrawQuantity}
            onChange={(e) => setWithdrawQuantity(e.target.value)}
            placeholder="Qty"
            className="dashboard-field"
          />
          <input
            type="text"
            value={withdrawNote}
            onChange={(e) => setWithdrawNote(e.target.value)}
            placeholder="Reason (optional)"
            className="dashboard-field"
          />
          <button
            type="submit"
            disabled={!canWithdraw || lockHeldByOtherUser}
            className="btn-primary disabled:opacity-50 disabled:cursor-not-allowed"
          >
            {instantWithdrawals ? `Withdraw ${formatPHP(selectedWithdrawAmount)}` : `Request ${formatPHP(selectedWithdrawAmount)}`}
          </button>
        </form>
        <div className="mt-3 grid grid-cols-2 sm:grid-cols-3 gap-2 text-xs font-inter text-gray-700">
          {withdrawDenominations.map((option) => {
            const count = machineInventory?.[option.field] ?? 0
            return (
              <div key={option.field} className="dashboard-list-item px-3 py-2">
                <p className="font-semibold text-gray-800">{option.label}</p>
                <p>{inventoryLoading ? 'Loading inventory...' : `${count} available`}</p>
              </div>
            )
          })}
        </div>
        <p className="text-sm text-gray-600 font-inter mt-2">
          Selected payout: <span className="font-semibold text-blue-700">{formatPHP(selectedWithdrawAmount)}</span>
        </p>
        <div className="mt-4 rounded-2xl border border-white/80 bg-white/72 p-4 shadow-sm">
          <div className="flex items-center justify-between gap-3 flex-wrap">
            <div>
              <h4 className="text-lg font-sora font-bold text-blue-700">Cash Stock</h4>
              <p className="text-xs text-gray-600 font-inter">What is currently loaded in the machine</p>
            </div>
            <div className="rounded-xl bg-white/80 px-4 py-2 border border-white shadow-sm">
              <p className="text-[11px] uppercase tracking-[0.2em] text-blue-600 font-inter font-semibold">Total value</p>
              <p className="text-xl font-sora font-black text-blue-700">{formatPHP(machineInventoryTotalValue)}</p>
            </div>
          </div>
          <div className="mt-3 grid grid-cols-2 sm:grid-cols-3 gap-2">
            {machineCashStock.map((item) => (
              <div key={item.field} className="dashboard-list-item">
                <p className="text-sm font-semibold text-gray-800">{item.label}</p>
                <p className="text-xs text-gray-600 font-inter">{inventoryLoading ? 'Loading...' : `${item.count} in stock`}</p>
              </div>
            ))}
          </div>
        </div>
        {!instantWithdrawals && (
          <p className="text-sm text-amber-700 font-inter mt-2">Parent approval is required before this withdrawal is processed.</p>
        )}
        {!canWithdraw && !inventoryLoading && (
          <p className="text-sm text-red-600 font-inter mt-2">
            Pick a denomination that is in stock, keep the quantity valid, and make sure the total stays within your balance.
          </p>
        )}
      </div>
      )}

      {kidQuickSection === 'activity' && (
      <div className="dashboard-panel">
        <h3 className="text-lg sm:text-xl font-sora font-bold text-gray-900 mb-3">Recent Activity</h3>
        <div className="space-y-2 font-inter text-gray-700 text-sm">
          {kidHistory.slice(0, 5).map((item) => (
            <div key={item.id} className="dashboard-list-item flex items-center justify-between px-3 py-2">
              <div>
                <p className="font-semibold text-gray-800">{item.note}</p>
                <p className="text-xs text-gray-500">{item.when}</p>
              </div>
              <p className={`font-sora font-bold ${getSignedTransactionAmount(item) >= 0 ? 'text-green-600' : 'text-red-600'}`}>
                {formatSignedPHP(getSignedTransactionAmount(item))}
              </p>
            </div>
          ))}
          {kidHistory.length === 0 && <p>No transactions yet.</p>}
        </div>
        <div className="mt-3 flex gap-2">
          <button
            type="button"
            onClick={() => setActiveMenu('settings')}
              className="dashboard-action-soft px-3 py-2 rounded-xl"
          >
            Open Settings
          </button>
          <button
            type="button"
            onClick={() => setKidQuickSection('none')}
              className="dashboard-action-soft px-3 py-2 rounded-xl"
          >
            Hide
          </button>
        </div>
      </div>
      )}
    </section>
  )

  const kidGoalsView = (
    <section className="space-y-6">
      <div className="grid md:grid-cols-3 gap-4">
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Total Saved</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">{formatPHP(totalGoalSaved)}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Goal Target</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">{formatPHP(totalGoalTarget)}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Completion</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">{totalGoalProgress}%</p>
        </div>
      </div>

      <div className="glass-card">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Add New Goal</h3>
        <form onSubmit={handleAddGoal} className="grid gap-3 md:grid-cols-3">
          <input
            type="text"
            value={newGoalName}
            onChange={(e) => setNewGoalName(e.target.value)}
            placeholder="Goal name"
            className="px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter bg-white/80"
          />
          <input
            type="number"
            min="1"
            step="0.01"
            value={newGoalTarget}
            onChange={(e) => setNewGoalTarget(e.target.value)}
            placeholder="Target amount"
            className="px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter bg-white/80"
          />
          <button type="submit" className="btn-primary">Add Goal</button>
        </form>
      </div>

      <div className="glass-card">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Savings Goals</h3>
        <div className="space-y-5">
          {kidGoals.map((goal) => {
            const percent = Math.min(100, Math.round((goal.saved / goal.target) * 100))
            const remaining = goal.target - goal.saved
            const goalMet = balance >= goal.target
            return (
              <div key={goal.id} className={`rounded-xl p-4 border-2 ${goalMet ? 'bg-emerald-50 border-emerald-300 shadow-[0_8px_22px_rgba(16,185,129,0.2)]' : 'bg-white/70 border-transparent'}`}>
                <div className="flex items-start justify-between gap-3 text-sm font-inter font-semibold text-gray-700 mb-1">
                  <span>{goal.name}</span>
                  <div className="flex items-center gap-2">
                    <span>{formatPHP(goal.saved)} / {formatPHP(goal.target)}</span>
                    <button
                      type="button"
                      onClick={() => { void requestGoalWithdrawal(goal) }}
                      disabled={balance < goal.target}
                      className="dashboard-action-secondary px-3 py-1 rounded-lg disabled:opacity-50 disabled:cursor-not-allowed"
                    >
                      Withdraw
                    </button>
                  </div>
                </div>
                <div className="w-full h-3 bg-white/60 rounded-full overflow-hidden mt-2">
                  <div className={`h-full ${goalMet ? 'bg-gradient-to-r from-emerald-500 to-green-600' : 'bg-gradient-to-r from-blue-600 to-teal-500'}`} style={{ width: `${percent}%` }}></div>
                </div>
                <div className="mt-3 flex items-center justify-between text-xs font-inter text-gray-600">
                  <span>{goalMet ? 'Goal reached' : `${percent}% complete`}</span>
                  <span>{balance < goal.target ? `Need ${formatPHP(goal.target - balance)} more balance to request` : `${formatPHP(remaining)} to go`}</span>
                </div>
              </div>
            )
          })}
        </div>
      </div>

      <div className="grid lg:grid-cols-2 gap-6">
        <div className="glass-card">
          <h4 className="text-xl font-sora font-bold text-blue-700 mb-3">Goal Ideas</h4>
          <div className="space-y-2 font-inter text-gray-700">
            <p>🎮 Save for a game accessory</p>
            <p>🎧 Upgrade study headphones</p>
            <p>📚 Build a mini book budget</p>
            <p>🏀 Sports gear target challenge</p>
          </div>
        </div>

        <div className="glass-card">
          <h4 className="text-xl font-sora font-bold text-blue-700 mb-3">Reach Goals Faster</h4>
          <div className="space-y-2 font-inter text-gray-700">
            <p>• Keep each withdrawal note short and clear.</p>
            <p>• Try to keep one no-spend day each week.</p>
            <p>• Check progress every 2–3 days.</p>
            <p>• Focus on one main goal at a time.</p>
          </div>
        </div>
      </div>
    </section>
  )

  const kidTransactionsView = (
    <section className="grid lg:grid-cols-2 gap-6">
      <div className="glass-card">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Transaction History</h3>
        <div className="space-y-3">
          {kidHistory.slice(0, 6).map((item) => (
            <div key={item.id} className="bg-white/70 rounded-xl px-4 py-3 flex items-center justify-between">
              <div>
                <p className="font-inter font-semibold text-gray-800">{item.note}</p>
                <p className="text-xs text-gray-600 font-inter">{item.when}</p>
              </div>
              <p className={`font-sora font-bold ${getSignedTransactionAmount(item) >= 0 ? 'text-green-600' : 'text-red-600'}`}>
                {formatSignedPHP(getSignedTransactionAmount(item))}
              </p>
            </div>
          ))}
          {kidHistory.length === 0 && (
            <p className="font-inter text-gray-700">No transactions yet.</p>
          )}
        </div>
      </div>

      <div className="glass-card">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Pending Authorization</h3>
        <div className="space-y-3">
          {pendingForKid.length === 0 ? (
            <p className="font-inter text-gray-700">No pending requests.</p>
          ) : (
            pendingForKid.map((item) => (
              <div key={item.id} className="bg-white/70 rounded-xl px-4 py-3">
                <p className="font-inter font-semibold text-gray-800">{formatPHP(item.amount)} • {item.note}</p>
                <p className="text-xs text-gray-600 font-inter">{item.createdAt}</p>
              </div>
            ))
          )}
        </div>
      </div>
    </section>
  )

  const kidStatisticsView = (
    <section className="space-y-6">
      <div className="grid md:grid-cols-2 xl:grid-cols-4 gap-6">
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Withdrawals</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">{kidHistory.length}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Total Spent</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">{formatPHP(totalKidSpent)}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Average Withdrawal</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">{formatPHP(kidAverageWithdrawal)}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Largest Withdrawal</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">{formatPHP(kidLargestWithdrawal)}</p>
        </div>
      </div>

      <div className="grid lg:grid-cols-2 gap-6">
        <div className="glass-card">
          <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Spending by Category</h3>
          <div className="space-y-3">
            {kidSpendingByNote.length === 0 ? (
              <p className="font-inter text-gray-700">No spending data yet.</p>
            ) : (
              kidSpendingByNote.map(([label, amount]) => {
                const percentage = totalKidSpent > 0 ? Math.round((amount / totalKidSpent) * 100) : 0
                return (
                  <div key={label}>
                    <div className="flex justify-between text-xs font-inter text-gray-700 mb-1">
                      <span>{label}</span>
                      <span>{formatPHP(amount)} ({percentage}%)</span>
                    </div>
                    <div className="w-full h-3 bg-white/60 rounded-full overflow-hidden">
                      <div className="h-full bg-gradient-to-r from-blue-600 to-teal-500" style={{ width: `${percentage}%` }}></div>
                    </div>
                  </div>
                )
              })
            )}
          </div>
        </div>

        <div className="glass-card">
          <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Goal Progress Graph</h3>
          <div className="space-y-3">
            {kidGoals.map((goal) => {
              const percent = Math.min(100, Math.round((goal.saved / goal.target) * 100))
              return (
                <div key={goal.id}>
                  <div className="flex justify-between text-xs font-inter text-gray-700 mb-1">
                    <span>{goal.name}</span>
                    <span>{percent}%</span>
                  </div>
                  <div className="w-full h-3 bg-white/60 rounded-full overflow-hidden">
                    <div className="h-full bg-gradient-to-r from-cyan-500 to-blue-600" style={{ width: `${percent}%` }}></div>
                  </div>
                </div>
              )
            })}
          </div>
        </div>
      </div>

      <div className="grid lg:grid-cols-2 gap-6">
        <div className="glass-card">
          <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Goal Progress Stats</h3>
          <div className="space-y-3 font-inter text-gray-700">
            <p>• Goals active: <span className="font-semibold">{kidGoals.length}</span></p>
            <p>• Combined progress: <span className="font-semibold">{totalGoalProgress}%</span></p>
            <p>• Top goal: <span className="font-semibold">{topGoal ? topGoal.name : 'No goal yet'}</span></p>
            <p>• Total saved toward goals: <span className="font-semibold">{formatPHP(totalGoalSaved)}</span></p>
          </div>
        </div>

        <div className="glass-card">
          <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Approval & Activity</h3>
          <div className="space-y-3 font-inter text-gray-700">
            <p>• Pending requests: <span className="font-semibold">{pendingForKid.length}</span></p>
            <p>• Approval completion rate: <span className="font-semibold">{kidApprovalRate}%</span></p>
            <p>• Current policy: <span className="font-semibold">{instantWithdrawals ? 'Instant mode' : 'Parent approval mode'}</span></p>
          </div>
        </div>
      </div>
    </section>
  )

  const kidSettingsView = (
    <section className="space-y-6">
      <div className="dashboard-panel">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-3">Settings Locked</h3>
        <p className="font-inter text-gray-700">
          Only parents can access and change settings. If you need to update limits or permissions,
          ask a parent to open the Parent Settings page.
        </p>
      </div>
      <div className="dashboard-panel">
        <p className="font-inter font-semibold text-gray-800">Current policy</p>
        <p className="text-sm text-gray-700 font-inter mt-1">
          {instantWithdrawals
            ? 'Immediate withdrawals are currently enabled by parent settings.'
            : 'Parent authorization is currently required before withdrawals are processed.'}
        </p>
      </div>
    </section>
  )

  const kidProfileView = (
    <section className="space-y-6">
      <div className="dashboard-panel">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Profile</h3>
        <div className="dashboard-list-item font-inter">
          <div className="flex items-center gap-4 mb-4">
            <div className="text-5xl">{selectedCharacter.emoji}</div>
            <div className="flex-1">
              <p className="font-semibold text-gray-700 mb-1">Username</p>
              <p className="text-2xl font-bold text-gray-800">{kidName || 'Guest'}</p>
            </div>
          </div>
          <div className="space-y-1 text-gray-700">
            <p><span className="font-semibold">Role:</span> Kid</p>
            <p><span className="font-semibold">Title:</span> {selectedCharacter.title}</p>
          </div>
        </div>
      </div>

      <div className="dashboard-panel">
        <h4 className="text-xl font-sora font-bold text-blue-700 mb-3">Choose Your Character</h4>
        <div className="grid grid-cols-2 md:grid-cols-3 gap-3">
          {characterOptions.map((option) => (
            <button
              key={option.id}
              type="button"
              onClick={() => setKidCharacter(option.id)}
              className={`rounded-xl p-3 text-left transition-all border-2 ${
                kidCharacter === option.id
                  ? 'border-blue-400 bg-white shadow-md shadow-blue-100'
                  : 'border-white bg-sky-50/75 hover:bg-white'
              }`}
            >
              <p className="text-3xl">{option.emoji}</p>
              <p className="font-sora font-semibold text-blue-700 mt-1">{option.title}</p>
            </button>
          ))}
        </div>
      </div>

      <div className="dashboard-panel">
        <h4 className="text-xl font-sora font-bold text-blue-700 mb-3">Edit Profile</h4>
        <div className="grid gap-3">
          <input
            type="text"
            value={profileState.username}
            onChange={(e) => setProfileState((prev) => ({ ...prev, username: e.target.value }))}
            placeholder="Username"
            className="dashboard-field w-full"
          />
          <input
            type="email"
            value={profileState.email}
            onChange={(e) => setProfileState((prev) => ({ ...prev, email: e.target.value }))}
            placeholder="Gmail / Email"
            className="dashboard-field w-full"
          />
          <input
            type="password"
            value={profileState.password}
            onChange={(e) => setProfileState((prev) => ({ ...prev, password: e.target.value }))}
            placeholder="New Password (leave blank to keep current)"
            className="dashboard-field w-full"
          />
          <input
            type="text"
            value={profileState.securityAnswer}
            onChange={(e) => setProfileState((prev) => ({ ...prev, securityAnswer: e.target.value }))}
            placeholder="Security Question Answer"
            className="dashboard-field w-full"
          />
          <button
            type="button"
            onClick={() => { void saveProfile() }}
            disabled={profileSaving}
            className="dashboard-action-primary px-4 py-2 disabled:opacity-50"
          >
            {profileSaving ? 'Saving...' : 'Save Profile'}
          </button>
          {profileMessage && (
            <p className={`text-sm font-inter ${profileMessage.kind === 'ok' ? 'text-green-700' : 'text-red-700'}`}>
              {profileMessage.text}
            </p>
          )}
        </div>
      </div>

      <div className="grid md:grid-cols-3 gap-4">
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Goals Active</p>
          <p className="text-3xl font-sora font-black text-blue-700 mt-2">{kidGoals.length}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Completed Activity</p>
          <p className="text-3xl font-sora font-black text-blue-700 mt-2">{kidHistory.length}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Goal Progress</p>
          <p className="text-3xl font-sora font-black text-blue-700 mt-2">{totalGoalProgress}%</p>
        </div>
      </div>
    </section>
  )

  const parentDashboardView = (
    <section className="space-y-4 sm:space-y-6">
      <div className="relative overflow-hidden rounded-2xl border-2 border-blue-200 bg-gradient-to-r from-blue-600 via-sky-500 to-teal-500 p-5 sm:p-6 shadow-lg">
        <div className="pointer-events-none absolute inset-0 bg-[radial-gradient(circle_at_12%_20%,rgba(255,255,255,0.24),transparent_34%),linear-gradient(180deg,rgba(255,255,255,0.08),transparent)]" />
        <div className="relative z-10 max-w-sm">
        <p className="text-[11px] uppercase tracking-[0.22em] text-white/80 font-inter font-semibold">Parent Wallet</p>
        <h2 className="text-4xl sm:text-5xl font-sora font-black text-white mt-2 drop-shadow-[0_6px_18px_rgba(11,49,96,0.34)]">{formatPHP(parentBalance)}</h2>
        <p className="text-sm text-white/88 font-inter mt-3 leading-relaxed">Kids: {validKidAccounts.length} • Pending requests: {parentPending.length}</p>
        </div>
      </div>

      <div className="grid grid-cols-2 sm:grid-cols-4 gap-2">
        <button
          type="button"
          onClick={() => setParentQuickSection((prev) => prev === 'deposit' ? 'none' : 'deposit')}
          className="dashboard-action-primary"
        >
          <span className="sm:hidden text-lg">💰</span>
          <span className="hidden sm:inline">Deposit</span>
        </button>
        <button
          type="button"
          onClick={() => setParentQuickSection((prev) => prev === 'withdraw' ? 'none' : 'withdraw')}
          className="dashboard-action-secondary"
        >
          <span className="sm:hidden text-lg">💸</span>
          <span className="hidden sm:inline">Withdraw</span>
        </button>
        <button
          type="button"
          onClick={() => setShowParentMoreActions((prev) => !prev)}
          className="dashboard-action-soft"
        >
          <span className="sm:hidden text-lg">⋯</span>
          <span className="hidden sm:inline">{showParentMoreActions ? 'Less' : 'More'}</span>
        </button>
      </div>

      {showParentMoreActions && (
      <div className="grid grid-cols-2 sm:grid-cols-3 gap-2">
        <button
          type="button"
          onClick={() => setParentQuickSection((prev) => prev === 'activity' ? 'none' : 'activity')}
          className="dashboard-action-soft"
        >
          <span className="sm:hidden text-lg">🧾</span>
          <span className="hidden sm:inline">Activity</span>
        </button>
        <button
          type="button"
          onClick={() => setActiveMenu('settings')}
          className="dashboard-action-soft"
        >
          <span className="sm:hidden text-lg">⚙️</span>
          <span className="hidden sm:inline">Settings</span>
        </button>
        <button
          type="button"
          onClick={() => setActiveMenu('profile')}
          className="dashboard-action-soft"
        >
          <span className="sm:hidden text-lg">👤</span>
          <span className="hidden sm:inline">Profile</span>
        </button>
      </div>
      )}

      {parentQuickSection === 'deposit' && (
      <div className="dashboard-panel">
        <h3 className="text-lg sm:text-xl font-sora font-bold text-gray-900 mb-3">Choose Account to Deposit</h3>
        {validKidAccounts.length === 0 && (
          <p className="text-sm text-gray-600 font-inter mb-3">No kid accounts yet. Create one in Settings.</p>
        )}
        <div className="grid sm:grid-cols-2 gap-3">
          {parentDepositTargets.map((username) => (
            <button
              key={username}
              type="button"
              onClick={() => startParentDepositFlow(username)}
              disabled={pendingDepositKid !== null || lockHeldByOtherUser}
              className="dashboard-list-item text-left font-inter font-semibold text-gray-800 disabled:opacity-50"
            >
              {username === parentName ? `Deposit to ${username} (Parent)` : `Deposit to ${username}`}
            </button>
          ))}
        </div>
      </div>
      )}

      {parentQuickSection === 'withdraw' && parentName && (
      <div className="dashboard-panel space-y-4">
        <h3 className="text-lg sm:text-xl font-sora font-bold text-gray-900">Withdraw Cash</h3>
        <div>
          <p className="font-inter font-semibold text-gray-800 mb-2">From Parent Wallet</p>
          <form onSubmit={handleParentWithdraw} className="grid gap-2 sm:grid-cols-4">
            <select
              value={withdrawDenomination}
              onChange={(e) => setWithdrawDenomination(e.target.value as WithdrawDenominationKey)}
              className="dashboard-field"
            >
              {withdrawDenominations.map((option) => {
                const count = machineInventory?.[option.field] ?? 0
                return (
                  <option key={option.field} value={option.field}>
                    {option.label} - {inventoryLoading ? 'loading...' : `${count} in stock`}
                  </option>
                )
              })}
            </select>
            <input
              type="number"
              min="1"
              step="1"
              value={withdrawQuantity}
              onChange={(e) => setWithdrawQuantity(e.target.value)}
              placeholder="Qty"
              className="dashboard-field"
            />
            <input
              type="text"
              value={parentWithdrawNote}
              onChange={(e) => setParentWithdrawNote(e.target.value)}
              placeholder="Reason"
              className="dashboard-field"
            />
            <button
              type="submit"
              disabled={!canParentWithdraw || parentWithdrawBusy || lockHeldByOtherUser}
              className="dashboard-action-secondary px-4 py-2 disabled:opacity-50"
            >
              {parentWithdrawBusy ? 'Processing...' : `Withdraw ${formatPHP(selectedWithdrawAmount)}`}
            </button>
          </form>
        </div>

        <div>
          <p className="font-inter font-semibold text-gray-800 mb-2">From Child Balance</p>
          <form onSubmit={handleParentChildWithdraw} className="grid gap-2 sm:grid-cols-5">
            <select
              value={parentChildWithdrawKid}
              onChange={(e) => setParentChildWithdrawKid(e.target.value)}
              className="dashboard-field"
            >
              {validKidAccounts.map((username) => (
                <option key={username} value={username}>{username}</option>
              ))}
            </select>
            <select
              value={withdrawDenomination}
              onChange={(e) => setWithdrawDenomination(e.target.value as WithdrawDenominationKey)}
              className="dashboard-field"
            >
              {withdrawDenominations.map((option) => {
                const count = machineInventory?.[option.field] ?? 0
                return (
                  <option key={option.field} value={option.field}>
                    {option.label} - {inventoryLoading ? 'loading...' : `${count} in stock`}
                  </option>
                )
              })}
            </select>
            <input
              type="number"
              min="1"
              step="1"
              value={withdrawQuantity}
              onChange={(e) => setWithdrawQuantity(e.target.value)}
              placeholder="Qty"
              className="dashboard-field"
            />
            <input
              type="text"
              value={parentChildWithdrawNote}
              onChange={(e) => setParentChildWithdrawNote(e.target.value)}
              placeholder="Reason"
              className="dashboard-field"
            />
            <button
              type="submit"
              disabled={!canParentChildWithdraw || parentChildWithdrawBusy || lockHeldByOtherUser}
              className="dashboard-action-secondary px-4 py-2 disabled:opacity-50"
            >
              {parentChildWithdrawBusy ? 'Processing...' : `Withdraw ${formatPHP(selectedWithdrawAmount)}`}
            </button>
          </form>
        </div>
      </div>
      )}

      {parentQuickSection === 'activity' && (
      <div className="grid lg:grid-cols-2 gap-4">
        <div className="dashboard-panel">
          <h3 className="text-lg font-sora font-bold text-gray-900 mb-2">Pending Requests</h3>
          <div className="space-y-2">
            {parentPending.slice(0, 6).map((item) => (
              <div key={item.id} className="dashboard-list-item px-3 py-2">
                <p className="font-inter font-semibold text-gray-800">{item.child} - {formatPHP(item.amount)}</p>
                <div className="mt-2 flex gap-2">
                  <button type="button" onClick={() => approvePending(item.id)} className="dashboard-action-secondary px-3 py-1 text-sm rounded-xl">Approve</button>
                  <button type="button" onClick={() => declinePending(item.id)} className="dashboard-action-soft px-3 py-1 text-sm rounded-xl">Decline</button>
                </div>
              </div>
            ))}
            {parentPending.length === 0 && <p className="text-sm text-gray-600 font-inter">No pending requests.</p>}
          </div>
        </div>

        <div className="dashboard-panel">
          <h3 className="text-lg font-sora font-bold text-gray-900 mb-2">Kid Balances</h3>
          <div className="space-y-2">
            {parentChildren.map((child) => (
              <div key={child.id} className="dashboard-list-item flex items-center justify-between px-3 py-2">
                <p className="font-inter font-semibold text-gray-800">{child.name}</p>
                <p className="font-sora font-bold text-gray-900">{formatPHP(child.balance)}</p>
              </div>
            ))}
            {parentChildren.length === 0 && <p className="text-sm text-gray-600 font-inter">No kid accounts yet.</p>}
          </div>
        </div>
      </div>
      )}
    </section>
  )

  const parentGoalsView = (
    <section className="space-y-6">
      <div className="grid md:grid-cols-3 gap-4">
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Your Goal Saved</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">
            {formatPHP(parentOwnGoals.reduce((sum, goal) => sum + goal.saved, 0))}
          </p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Your Goal Target</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">
            {formatPHP(parentOwnGoals.reduce((sum, goal) => sum + goal.target, 0))}
          </p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Your Completion</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">
            {(() => {
              const saved = parentOwnGoals.reduce((sum, goal) => sum + goal.saved, 0)
              const target = parentOwnGoals.reduce((sum, goal) => sum + goal.target, 0)
              return `${target > 0 ? Math.round((saved / target) * 100) : 0}%`
            })()}
          </p>
        </div>
      </div>

      <div className="glass-card">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Add Your Goal</h3>
        <form onSubmit={handleAddParentGoal} className="grid gap-3 md:grid-cols-3">
          <input
            type="text"
            value={newParentGoalName}
            onChange={(e) => setNewParentGoalName(e.target.value)}
            placeholder="Goal name"
            className="px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter bg-white/80"
          />
          <input
            type="number"
            min="1"
            step="0.01"
            value={newParentGoalTarget}
            onChange={(e) => setNewParentGoalTarget(e.target.value)}
            placeholder="Target amount"
            className="px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter bg-white/80"
          />
          <button type="submit" className="btn-primary">Add Goal</button>
        </form>
      </div>

      <div className="glass-card">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Your Goals</h3>
        <div className="space-y-4">
          {parentOwnGoals.length === 0 ? (
            <p className="font-inter text-gray-700">No parent goals yet.</p>
          ) : parentOwnGoals.map((goal) => {
            const percent = Math.min(100, Math.round((goal.saved / goal.target) * 100))
            const goalMet = parentBalance >= goal.target
            return (
              <div key={goal.id} className={`rounded-xl p-4 border-2 ${goalMet ? 'bg-emerald-50 border-emerald-300 shadow-[0_8px_22px_rgba(16,185,129,0.2)]' : 'bg-white/70 border-transparent'}`}>
                <div className="flex justify-between text-sm font-inter font-semibold text-gray-700 mb-1">
                  <span>{goal.name}</span>
                  <span>{formatPHP(goal.saved)} / {formatPHP(goal.target)}</span>
                </div>
                <div className="w-full h-3 bg-white/60 rounded-full overflow-hidden mt-2">
                  <div className={`h-full ${goalMet ? 'bg-gradient-to-r from-emerald-500 to-green-600' : 'bg-gradient-to-r from-blue-600 to-teal-500'}`} style={{ width: `${percent}%` }}></div>
                </div>
              </div>
            )
          })}
        </div>
      </div>

      <div className="glass-card">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Kids Goal Progress</h3>
        <div className="space-y-5">
          {parentGoals.length === 0 ? (
            <p className="font-inter text-gray-700">No goals created yet.</p>
          ) : parentGoals.map((goal) => {
            const percent = Math.min(100, Math.round((goal.saved / goal.target) * 100))
            return (
              <div key={goal.key}>
                <div className="flex justify-between text-sm font-inter font-semibold text-gray-700 mb-1">
                  <span>{goal.child} • {goal.name}</span>
                  <span>{percent}%</span>
                </div>
                <div className="w-full h-3 bg-white/60 rounded-full overflow-hidden">
                  <div className="h-full bg-gradient-to-r from-blue-600 to-teal-500" style={{ width: `${percent}%` }}></div>
                </div>
              </div>
            )
          })}
        </div>
      </div>
    </section>
  )

  const parentTransactionsView = (
    <section className="grid lg:grid-cols-2 gap-6">
      <div className="glass-card">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Pending Authorization</h3>
        <div className="space-y-3">
          {parentPending.length === 0 ? (
            <p className="font-inter text-gray-700">No pending requests.</p>
          ) : (
            parentPending.map((item) => (
              <div key={item.id} className="bg-white/70 rounded-xl px-4 py-3">
                <p className="font-inter font-semibold text-gray-800">{item.child} • {formatPHP(item.amount)}</p>
                <p className="text-xs text-gray-600 font-inter mb-3">{item.note} • {item.createdAt}</p>
                <div className="flex gap-2">
                  <button
                    type="button"
                    onClick={() => approvePending(item.id)}
                    disabled={lockHeldByOtherUser}
                    className="px-3 py-2 rounded-lg bg-gradient-to-r from-blue-600 to-teal-500 text-white font-inter font-semibold"
                  >
                    Approve
                  </button>
                  <button
                    type="button"
                    onClick={() => declinePending(item.id)}
                    className="px-3 py-2 rounded-lg bg-white text-gray-700 border border-gray-300 font-inter font-semibold"
                  >
                    Decline
                  </button>
                </div>
              </div>
            ))
          )}
        </div>
      </div>

      <div className="glass-card">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Transaction History</h3>
        <div className="space-y-3">
          {history.slice(0, 8).map((entry) => (
            <div key={entry.id} className="bg-white/70 rounded-xl px-4 py-3 flex items-center justify-between">
              <div>
                <p className="font-inter font-semibold text-gray-800">{entry.child} • {entry.note}</p>
                <p className="text-xs text-gray-600 font-inter">{entry.when}</p>
              </div>
              <p className={`font-sora font-bold ${getSignedTransactionAmount(entry) >= 0 ? 'text-green-600' : 'text-red-600'}`}>
                {formatSignedPHP(getSignedTransactionAmount(entry))}
              </p>
            </div>
          ))}
        </div>
      </div>
    </section>
  )

  const parentStatisticsView = (
    <section className="space-y-6">
      <div className="grid md:grid-cols-2 xl:grid-cols-4 gap-6">
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Total Withdrawals</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">{outgoingHistory.length}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Total Amount</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">
            {formatPHP(totalOutgoingHistoryAmount)}
          </p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Pending Requests</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">{parentPending.length}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Average Withdrawal</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">
            {formatPHP(outgoingHistory.length > 0 ? (totalOutgoingHistoryAmount / outgoingHistory.length) : 0)}
          </p>
        </div>
      </div>

      <div className="grid lg:grid-cols-2 gap-6">
        <div className="glass-card">
          <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Withdrawals by Child (Graph)</h3>
          <div className="space-y-3">
            {parentSpendingByChild.map((row) => {
              const maxAmount = Math.max(...parentSpendingByChild.map((item) => item.amount), 1)
              const widthPercent = Math.round((row.amount / maxAmount) * 100)
              return (
                <div key={row.name}>
                  <div className="flex justify-between text-xs font-inter text-gray-700 mb-1">
                    <span>{row.name}</span>
                    <span>{formatPHP(row.amount)}</span>
                  </div>
                  <div className="w-full h-3 bg-white/60 rounded-full overflow-hidden">
                    <div className="h-full bg-gradient-to-r from-blue-600 to-teal-500" style={{ width: `${widthPercent}%` }}></div>
                  </div>
                </div>
              )
            })}
          </div>
        </div>

        <div className="glass-card">
          <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Approval Flow (Graph)</h3>
          <div className="space-y-3">
            <div>
              <div className="flex justify-between text-xs font-inter text-gray-700 mb-1">
                <span>Approved/Completed</span>
                <span>{history.length}</span>
              </div>
              <div className="w-full h-3 bg-white/60 rounded-full overflow-hidden">
                <div
                  className="h-full bg-gradient-to-r from-cyan-500 to-blue-600"
                  style={{
                    width: `${Math.round((history.length / Math.max(history.length + parentPending.length, 1)) * 100)}%`,
                  }}
                ></div>
              </div>
            </div>

            <div>
              <div className="flex justify-between text-xs font-inter text-gray-700 mb-1">
                <span>Pending</span>
                <span>{parentPending.length}</span>
              </div>
              <div className="w-full h-3 bg-white/60 rounded-full overflow-hidden">
                <div
                  className="h-full bg-gradient-to-r from-amber-400 to-orange-500"
                  style={{
                    width: `${Math.round((parentPending.length / Math.max(history.length + parentPending.length, 1)) * 100)}%`,
                  }}
                ></div>
              </div>
            </div>
          </div>
        </div>
      </div>

      <div className="grid lg:grid-cols-2 gap-6">
        <div className="glass-card">
          <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Policy Snapshot</h3>
          <div className="space-y-2 font-inter text-gray-700">
            <p>• Instant withdrawals: <span className="font-semibold">{instantWithdrawals ? 'Enabled' : 'Disabled'}</span></p>
            <p>• Auto-approve limit: <span className="font-semibold">{formatPHP(parentAutoApproveLimit)}</span></p>
            <p>• Spending alerts: <span className="font-semibold">{parentSpendingAlerts ? 'On' : 'Off'}</span></p>
          </div>
        </div>

        <div className="glass-card">
          <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-4">Child Breakdown</h3>
          <div className="space-y-2 font-inter text-gray-700">
            {parentChildren.map((child) => (
              <p key={child.id}>
                • {child.name}: <span className="font-semibold">{formatPHP(child.balance)}</span> balance, {child.withdrawalsThisWeek} withdrawals
              </p>
            ))}
          </div>
        </div>
      </div>
    </section>
  )

  const parentSettingsView = (
    <section className="space-y-6">
      <div className="dashboard-panel space-y-5">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700">Parent Settings</h3>

        <div className="dashboard-list-item space-y-3">
          <div>
            <p className="font-inter font-semibold text-gray-800">📶 Device WiFi</p>
            <p className="text-sm text-gray-600 font-inter">
              Change the WiFi network the ATM device connects to. The new credentials are sent to the device on its next poll (~10 seconds) and saved permanently.
            </p>
            <p className="text-xs text-amber-700 font-inter mt-1">
              First-time setup / recovery: turn on a phone hotspot named <span className="font-mono">CASHWIFI</span> with password <span className="font-mono">CASH12345!</span>. The ATM auto-connects to this fallback network. Once it&apos;s online, use the form below to switch it onto your permanent WiFi.
            </p>
          </div>
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
            <input
              type="text"
              placeholder="WiFi SSID"
              value={wifiSsid}
              onChange={(e) => setWifiSsid(e.target.value)}
              maxLength={32}
              className="dashboard-field"
            />
            <input
              type="password"
              placeholder="WiFi Password"
              value={wifiPass}
              onChange={(e) => setWifiPass(e.target.value)}
              maxLength={63}
              className="dashboard-field"
            />
          </div>
          <div className="flex items-center gap-3">
            <button
              type="button"
              disabled={wifiSaving || wifiSsid.trim().length === 0}
              onClick={async () => {
                setWifiMessage(null)
                const ssid = wifiSsid.trim()
                const pass = wifiPass
                if (!ssid) return
                if (ssid.includes('"') || pass.includes('"')) {
                  setWifiMessage({ kind: 'err', text: 'SSID and password cannot contain double-quote (") characters.' })
                  return
                }
                setWifiSaving(true)
                try {
                  const command = `SETWIFI ssid="${ssid}" pass="${pass}"`
                  const res = await fetch('/api/command', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ command }),
                  })
                  const data = await res.json().catch(() => ({}))
                  if (!res.ok) {
                    setWifiMessage({ kind: 'err', text: (data && data.error) || `Request failed (${res.status})` })
                  } else {
                    setWifiMessage({ kind: 'ok', text: 'Sent. Device will reconnect within ~10 seconds.' })
                    setWifiPass('')
                  }
                } catch (err) {
                  setWifiMessage({ kind: 'err', text: err instanceof Error ? err.message : 'Network error' })
                } finally {
                  setWifiSaving(false)
                }
              }}
              className="dashboard-action-primary px-4 py-2 disabled:opacity-50"
            >
              {wifiSaving ? 'Sending…' : 'Send to Device'}
            </button>
            {wifiMessage && (
              <span className={`text-sm font-inter ${wifiMessage.kind === 'ok' ? 'text-green-700' : 'text-red-700'}`}>
                {wifiMessage.text}
              </span>
            )}
          </div>
        </div>

        <div className="dashboard-list-item flex items-center justify-between gap-4">
          <div>
            <p className="font-inter font-semibold text-gray-800">Allow immediate kid withdrawals</p>
            <p className="text-sm text-gray-600 font-inter">
              When enabled, kid withdrawals are processed instantly without authorization.
            </p>
          </div>
          <button
            type="button"
            onClick={() => setInstantWithdrawals((prev) => !prev)}
            className={`px-4 py-2 rounded-lg font-sora font-semibold transition-all ${
              instantWithdrawals
                ? 'bg-gradient-to-r from-blue-600 via-sky-500 to-cyan-400 text-white shadow-md'
                : 'bg-white text-gray-700 border border-gray-300 shadow-sm'
            }`}
          >
            {instantWithdrawals ? 'Enabled' : 'Disabled'}
          </button>
        </div>

        <div className="dashboard-list-item flex items-center justify-between gap-4">
          <div>
            <p className="font-inter font-semibold text-gray-800">Spending alerts</p>
            <p className="text-sm text-gray-600 font-inter">Show alerts for kid withdrawal activity and thresholds.</p>
          </div>
          <button
            type="button"
            onClick={() => setParentSpendingAlerts((prev) => !prev)}
            className={`px-4 py-2 rounded-lg font-sora font-semibold transition-all ${
              parentSpendingAlerts
                ? 'bg-gradient-to-r from-blue-600 via-sky-500 to-cyan-400 text-white shadow-md'
                : 'bg-white text-gray-700 border border-gray-300 shadow-sm'
            }`}
          >
            {parentSpendingAlerts ? 'On' : 'Off'}
          </button>
        </div>

        <div className="dashboard-list-item">
          <p className="font-inter font-semibold text-gray-800 mb-2">Auto-approve limit (PHP)</p>
          <div className="flex items-center gap-3">
            <input
              type="number"
              min="0"
              step="0.01"
              value={parentAutoApproveLimit}
              onChange={(e) => setParentAutoApproveLimit(Math.max(0, Number(e.target.value) || 0))}
              className="dashboard-field w-32"
            />
            <p className="text-sm text-gray-600 font-inter">
              Requests at or below this amount are auto-approved when instant mode is off.
            </p>
          </div>
        </div>

        <div className="border-t border-blue-200 pt-6">
          <h4 className="text-lg font-sora font-bold text-blue-700 mb-3">👶 Create Kid Accounts</h4>
          <div className="rounded-2xl border border-sky-200 bg-gradient-to-br from-sky-50 via-white to-cyan-50 p-4 mb-4 shadow-sm">
            <p className="text-sm text-blue-800 font-inter mb-4">
              Create accounts for your children. They will use these credentials to log in and access their savings dashboard.
            </p>
            <div className="space-y-3">
              <input
                type="text"
                value={newKidUsername}
                onChange={(e) => setNewKidUsername(e.target.value)}
                placeholder="Kid username (e.g., Sarah, Tommy)"
                className="dashboard-field w-full px-4 py-2"
              />
              <input
                type="password"
                value={newKidPassword}
                onChange={(e) => setNewKidPassword(e.target.value)}
                placeholder="Create a password for them"
                className="dashboard-field w-full px-4 py-2"
              />
              <input
                type="text"
                value={newKidSecurityQuestion === 'Custom question' ? newKidCustomQuestion : ''}
                onChange={(e) => setNewKidCustomQuestion(e.target.value)}
                placeholder="Your custom security question"
                className="dashboard-field w-full px-4 py-2"
              />
              <select
                value={newKidSecurityQuestion}
                onChange={(e) => setNewKidSecurityQuestion(e.target.value)}
                className="dashboard-field w-full px-4 py-2"
              >
                <option value="What is your favorite pet?">What is your favorite pet?</option>
                <option value="What is your favorite color?">What is your favorite color?</option>
                <option value="What is your favorite food?">What is your favorite food?</option>
                <option value="What city were you born in?">What city were you born in?</option>
                <option value="What is the name of your best friend?">What is the name of your best friend?</option>
                <option value="Custom question">Custom question</option>
              </select>
              {newKidSecurityQuestion === 'Custom question' && (
                <input
                  type="text"
                  value={newKidCustomQuestion}
                  onChange={(e) => setNewKidCustomQuestion(e.target.value)}
                  placeholder="Your custom security question"
                  className="dashboard-field w-full px-4 py-2"
                />
              )}
              <input
                type="text"
                value={newKidSecurityAnswer}
                onChange={(e) => setNewKidSecurityAnswer(e.target.value)}
                placeholder="🔐 Answer to security question"
                className="dashboard-field w-full px-4 py-2"
              />
              <button
                type="button"
                onClick={async () => {
                  if (!newKidUsername.trim() || !newKidPassword.trim() || !newKidSecurityAnswer.trim()) {
                    alert('Please enter username, password, and security answer')
                    return
                  }
                  const finalQuestion = newKidSecurityQuestion === 'Custom question' ? newKidCustomQuestion : newKidSecurityQuestion
                  if (!finalQuestion.trim()) {
                    alert('Please enter your custom security question!')
                    return
                  }
                  const username = newKidUsername.trim().toLowerCase()
                  if (validKidAccounts.some((account) => account.toLowerCase() === username)) {
                    alert('This username already exists!')
                    return
                  }

                  const res = await fetch('/api/auth/kids', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                      username,
                      password: newKidPassword,
                      securityQuestion: finalQuestion,
                      securityAnswer: newKidSecurityAnswer.trim().toLowerCase(),
                      parentUsername: parentName,
                    }),
                  })
                  if (!res.ok) {
                    const data = await res.json().catch(() => ({ error: 'Failed to create kid account' }))
                    alert(`❌ ${data.error ?? 'Failed to create kid account'}`)
                    return
                  }

                  const kidsRes = await fetch(`/api/auth/kids?parent=${encodeURIComponent(parentName)}`, { cache: 'no-store' })
                  if (kidsRes.ok) {
                    const kidsData = await kidsRes.json() as { kids: Array<{ username: string; balance: number }> }
                    setValidKidAccounts(kidsData.kids.map((kid) => kid.username))
                    setKidBalances(
                      kidsData.kids.reduce<Record<string, number>>((acc, kid) => {
                        acc[kid.username] = kid.balance
                        return acc
                      }, {})
                    )
                  }

                  // Clear the form
                  setNewKidUsername('')
                  setNewKidPassword('')
                  setNewKidSecurityQuestion("What's your favorite pet?")
                  setNewKidSecurityAnswer('')
                  setNewKidCustomQuestion('')
                  alert(`✅ Kid account "${username}" created successfully in database!`)
                }}
                className="dashboard-action-primary w-full py-2 px-4"
              >
                ➕ Create Kid Account
              </button>
            </div>
          </div>

          {validKidAccounts.length > 0 && (
            <div className="dashboard-list-item p-4">
              <p className="font-inter font-semibold text-gray-800 mb-3">Active Kid Accounts:</p>
              <div className="space-y-2">
                {validKidAccounts.map((username) => (
                  <div key={username} className="flex items-center justify-between bg-white rounded-lg p-3 border border-sky-100 shadow-sm">
                    <div className="flex-1">
                      <p className="font-inter font-bold text-gray-800">👶 {username}</p>
                      <p className="text-sm text-gray-600 font-inter">Balance: {formatPHP(kidBalances[username] || 0)}</p>
                    </div>
                    <button
                      type="button"
                      onClick={async () => {
                        if (window.confirm(`Delete kid account "${username}"? This cannot be undone.`)) {
                          const res = await fetch(`/api/auth/kids?username=${encodeURIComponent(username)}&parent=${encodeURIComponent(parentName)}`, {
                            method: 'DELETE',
                          })
                          if (!res.ok) {
                            const data = await res.json().catch(() => ({ error: 'Failed to delete kid account' }))
                            alert(`❌ ${data.error ?? 'Failed to delete kid account'}`)
                            return
                          }

                          const kidsRes = await fetch(`/api/auth/kids?parent=${encodeURIComponent(parentName)}`, { cache: 'no-store' })
                          if (kidsRes.ok) {
                            const kidsData = await kidsRes.json() as { kids: Array<{ username: string; balance: number }> }
                            setValidKidAccounts(kidsData.kids.map((kid) => kid.username))
                            setKidBalances(
                              kidsData.kids.reduce<Record<string, number>>((acc, kid) => {
                                acc[kid.username] = kid.balance
                                return acc
                              }, {})
                            )
                          }
                          alert(`✅ Kid account "${username}" has been deleted from database.`)
                        }
                      }}
                      className="text-red-600 hover:text-red-700 font-inter font-semibold text-sm transition-colors"
                    >
                      Delete
                    </button>
                  </div>
                ))}
              </div>
            </div>
          )}
        </div>

      </div>
    </section>
  )

  const parentProfileView = (
    <section className="dashboard-panel space-y-4">
      <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700">Profile</h3>
      <div className="dashboard-list-item font-inter">
        <p><span className="font-semibold">Username:</span> {parentName || 'Parent Account'}</p>
        <p><span className="font-semibold">Role:</span> Parent</p>
        <p><span className="font-semibold">Permissions:</span> View balances, approve withdrawals, manage settings</p>
      </div>
      <div className="space-y-3">
        <input
          type="text"
          value={profileState.username}
          onChange={(e) => setProfileState((prev) => ({ ...prev, username: e.target.value }))}
          placeholder="Username"
          className="dashboard-field w-full"
        />
        <input
          type="email"
          value={profileState.email}
          onChange={(e) => setProfileState((prev) => ({ ...prev, email: e.target.value }))}
          placeholder="Gmail / Email"
          className="dashboard-field w-full"
        />
        <input
          type="password"
          value={profileState.password}
          onChange={(e) => setProfileState((prev) => ({ ...prev, password: e.target.value }))}
          placeholder="New Password (leave blank to keep current)"
          className="dashboard-field w-full"
        />
        <input
          type="text"
          value={profileState.securityAnswer}
          onChange={(e) => setProfileState((prev) => ({ ...prev, securityAnswer: e.target.value }))}
          placeholder="Security Question Answer"
          className="dashboard-field w-full"
        />
        <button
          type="button"
          onClick={() => { void saveProfile() }}
          disabled={profileSaving}
          className="dashboard-action-primary px-4 py-2 disabled:opacity-50"
        >
          {profileSaving ? 'Saving...' : 'Save Profile'}
        </button>
        {profileMessage && (
          <p className={`text-sm font-inter ${profileMessage.kind === 'ok' ? 'text-green-700' : 'text-red-700'}`}>
            {profileMessage.text}
          </p>
        )}
      </div>
    </section>
  )

  const kidProgressView = (
    <section className="space-y-6">
      <div className="glass-card">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-2">Progress</h3>
        <p className="font-inter text-gray-700 text-sm">
          Add a goal with a target amount, then track completion as your balance grows.
          Goals automatically light up green when your current balance can fully cover the target.
        </p>
      </div>
      {kidGoalsView}
    </section>
  )

  const parentProgressView = (
    <section className="space-y-6">
      <div className="glass-card">
        <h3 className="text-xl sm:text-2xl font-sora font-bold text-blue-700 mb-2">Progress</h3>
        <p className="font-inter text-gray-700 text-sm">
          Track each child&apos;s goals and completion status in one place.
        </p>
      </div>
      {parentGoalsView}
    </section>
  )

  const kidContent: Record<MenuKey, JSX.Element> = {
    dashboard: kidDashboardView,
    progress: kidProgressView,
    transactions: kidTransactionsView,
    settings: kidSettingsView,
    profile: kidProfileView,
  }

  const parentContent: Record<MenuKey, JSX.Element> = {
    dashboard: parentDashboardView,
    progress: parentProgressView,
    transactions: parentTransactionsView,
    settings: parentSettingsView,
    profile: parentProfileView,
  }

  return (
    <div className="min-h-screen relative overflow-hidden bg-gradient-to-br from-blue-500 via-teal-400 to-cyan-300">
      <div className="pointer-events-none absolute inset-0">
        <div
          className="absolute inset-0 opacity-35"
          style={{
            backgroundImage:
              'radial-gradient(circle at 12% 18%, rgba(255,255,255,0.45) 0%, rgba(255,255,255,0) 36%), radial-gradient(circle at 85% 14%, rgba(173,255,252,0.45) 0%, rgba(173,255,252,0) 34%), radial-gradient(circle at 58% 82%, rgba(29,174,214,0.40) 0%, rgba(29,174,214,0) 40%)',
          }}
        />
        <div className="absolute top-20 left-10 w-72 h-72 bg-blue-400 rounded-full mix-blend-multiply filter blur-xl opacity-50 animate-float"></div>
        <div className="absolute top-40 right-20 w-96 h-96 bg-cyan-300 rounded-full mix-blend-multiply filter blur-xl opacity-50 animate-float" style={{ animationDelay: '0.4s' }}></div>
        <div className="absolute -bottom-8 left-1/3 w-80 h-80 bg-teal-400 rounded-full mix-blend-multiply filter blur-xl opacity-50 animate-float" style={{ animationDelay: '0.8s' }}></div>
        <div className="absolute top-1/4 left-1/4 text-6xl opacity-20 animate-bounce-slow">💰</div>
        <div className="absolute bottom-1/4 right-1/3 text-5xl opacity-20 animate-bounce-slow" style={{ animationDelay: '0.35s' }}>🪙</div>
        <div className="absolute top-[62%] left-[18%] text-4xl opacity-20 animate-bounce-slow" style={{ animationDelay: '0.65s' }}>💵</div>
      </div>

      <div className="relative z-10 min-h-screen flex items-center justify-center p-3 sm:p-4 md:p-6">
        <div className="w-full max-w-[1240px] rounded-3xl border border-white/60 bg-white/14 backdrop-blur-md shadow-[0_24px_70px_rgba(8,41,82,0.24)] lg:flex">
        <aside className="relative hidden lg:flex lg:flex-col w-72 m-3 mr-0 rounded-2xl border-2 border-blue-200 bg-white/92 shadow-[0_16px_34px_rgba(16,60,102,0.2)] overflow-hidden">
          <div className="pointer-events-none absolute inset-0">
            <div className="absolute -top-10 -left-8 h-44 w-44 rounded-full bg-blue-100/55 blur-2xl" />
            <div className="absolute bottom-0 right-0 h-40 w-40 rounded-full bg-teal-100/55 blur-2xl" />
            <div className="absolute inset-0 bg-gradient-to-b from-white/85 via-white/72 to-white/66" />
          </div>

          <div className="relative z-10 flex h-full flex-col p-5">
            <div className="mb-6 rounded-2xl border-2 border-blue-200 bg-white p-3 shadow-sm">
              <h1 className="text-4xl font-sora font-black leading-none tracking-tight text-transparent bg-clip-text bg-gradient-to-r from-blue-700 via-sky-600 to-teal-500 drop-shadow-[0_2px_6px_rgba(17,83,145,0.2)]">
                C.A.S.H.
              </h1>
              <p className="mt-2 text-[13px] text-slate-700/95 font-inter font-semibold tracking-wide">Learn | Save | Achieve</p>
            </div>

            <div className="mb-6 rounded-2xl border-2 border-blue-200 bg-white/80 p-3.5 shadow-sm">
              <p className="text-xs font-inter font-semibold text-slate-700/95 tracking-wide">
                View: <span className="font-sora text-blue-800">{role === 'kid' ? 'Kid' : 'Parent'}</span>
              </p>
            </div>

            <nav className="space-y-2">
              {visibleMenuItems.map((item) => (
                <button
                  key={item.key}
                  type="button"
                  onClick={() => setActiveMenu(item.key)}
                  className={`w-full flex items-center gap-3 px-3 py-2.5 rounded-xl text-left font-inter text-sm font-semibold transition-all duration-200 ${
                    activeMenu === item.key
                      ? 'bg-gradient-to-r from-blue-600 to-cyan-500 text-white border border-cyan-200 shadow-[0_10px_24px_rgba(10,82,152,0.28)]'
                      : 'text-slate-700 bg-white/75 hover:bg-white hover:text-slate-900 border-2 border-blue-200'
                  }`}
                >
                  <span
                    className={`grid h-7 w-7 place-items-center rounded-full border text-[12px] leading-none ${
                      activeMenu === item.key
                        ? 'bg-white text-blue-700 border-blue-100'
                        : 'bg-white text-slate-700 border-sky-200'
                    }`}
                  >
                    {item.icon}
                  </span>
                  <span className="tracking-wide">{item.label}</span>
                </button>
              ))}
            </nav>

            <div className="mt-auto pt-5">
              <button
                type="button"
                onClick={handleLogout}
                className="block w-full text-center rounded-xl bg-gradient-to-r from-blue-600 via-cyan-500 to-teal-400 text-white font-sora text-sm font-semibold py-2.5 shadow-[0_12px_28px_rgba(11,95,174,0.38)] hover:shadow-[0_16px_34px_rgba(11,95,174,0.44)] hover:brightness-110 transition-all"
              >
                Logout
              </button>
            </div>
          </div>
        </aside>

        <main className="flex-1 p-3 sm:p-4 md:p-5 lg:p-6">
          <div className="relative rounded-2xl border-2 border-blue-200 bg-white/88 shadow-[0_20px_44px_rgba(14,30,64,0.16)] p-3 sm:p-4 md:p-6">
            <div className="pointer-events-none absolute inset-0 rounded-2xl bg-gradient-to-b from-white/82 via-white/58 to-white/30" />
            <div className="relative z-10">
          <div className={`mb-4 rounded-2xl border px-4 py-3 shadow-[0_12px_26px_rgba(11,56,95,0.16)] ${deviceUsableForCurrentUser ? 'bg-gradient-to-r from-emerald-200/90 via-cyan-200/85 to-teal-200/85 border-emerald-300/85' : 'bg-gradient-to-r from-rose-200/92 via-orange-200/88 to-amber-200/88 border-rose-300/85'}`}>
            <p className={`font-sora font-bold ${deviceUsableForCurrentUser ? 'text-emerald-900' : 'text-rose-900'}`}>
              Device: {deviceUsableForCurrentUser ? 'Usable' : 'Not Usable'}
            </p>
            <p className={`text-xs font-inter mt-1 ${deviceUsableForCurrentUser ? 'text-emerald-900/85' : 'text-rose-900/85'}`}>
              {deviceUsableForCurrentUser
                ? 'You can start deposit or withdrawal.'
                : `Currently in use by ${deviceLockStatus.holder ?? 'another user'} (${deviceLockStatus.mode ?? 'operation'}).`}
            </p>
          </div>

          <div className="lg:hidden mb-4">
            <div className="rounded-2xl border-2 border-blue-200 bg-white/88 p-3 shadow-[0_12px_28px_rgba(14,30,64,0.12)]">
              <div className="flex items-center justify-between">
                <h1 className="text-2xl sm:text-3xl font-sora font-black text-slate-900">C.A.S.H.</h1>
                <span className="bg-white text-slate-900 px-3 py-2 rounded-xl font-sora font-semibold text-xs sm:text-sm border border-white shadow-sm">
                  {role === 'kid' ? 'Kid View' : 'Parent View'}
                </span>
              </div>
              <p className="mt-1 mb-3 text-[11px] font-inter font-semibold text-slate-700/90 tracking-wide">Learn | Save | Achieve</p>

              <div className="flex gap-1.5">
              {visibleMenuItems.map((item) => (
                <button
                  key={item.key}
                  type="button"
                  onClick={() => setActiveMenu(item.key)}
                  className={`flex-1 flex flex-col items-center py-2 px-1.5 rounded-xl font-inter font-semibold transition-all ${
                    activeMenu === item.key
                      ? 'bg-gradient-to-r from-blue-600 to-cyan-500 text-white border border-cyan-200 shadow-[0_10px_20px_rgba(10,82,152,0.35)]'
                      : 'bg-white/80 text-slate-800 border-2 border-blue-200'
                  }`}
                >
                  <span className={`grid h-6 w-6 place-items-center rounded-full border text-[11px] ${
                    activeMenu === item.key
                      ? 'border-white bg-white text-blue-700'
                      : 'border-slate-300 bg-white text-slate-700'
                  }`}>
                    {item.icon}
                  </span>
                  <span className="text-[10px] mt-1 leading-tight tracking-wide">{item.label}</span>
                </button>
              ))}
              </div>
            </div>
          </div>

          {role === 'kid' ? kidContent[activeMenu] : parentContent[activeMenu]}
            </div>
          </div>
        </main>
        </div>
      </div>

      {/* Puppy widget removed */}

      {depositToast && (
        <div className="fixed top-4 left-1/2 -translate-x-1/2 z-50 bg-green-500 text-white px-4 py-3 sm:px-6 sm:py-4 rounded-2xl shadow-2xl font-sora font-bold text-sm sm:text-xl flex items-center gap-2 sm:gap-3 animate-bounce max-w-[90vw] text-center">
          <span>💰</span>
          <span>{depositToast}</span>
        </div>
      )}

        {role === 'kid' && kidDepositModalOpen && (
          <div className="fixed inset-0 z-50 bg-black/40 backdrop-blur-sm flex items-center justify-center p-4">
            <div className="w-full max-w-lg bg-white rounded-2xl shadow-2xl p-6 space-y-4">
              <h3 className="text-2xl font-sora font-bold text-green-700">Deposit Cash</h3>
              <p className="font-inter text-gray-700">
                Insert coins or bills into the acceptor. The timer resets with each coin or bill detected.
              </p>

              <div className="flex items-center justify-center">
                <div className={`text-6xl font-sora font-black ${
                  depositCountdown <= 5 ? 'text-red-500' : depositCountdown <= 10 ? 'text-amber-500' : 'text-green-600'
                }`}>{depositCountdown}s</div>
              </div>

              <div className="bg-green-50 rounded-xl p-4 space-y-2 font-inter text-gray-800">
                <p>Collected so far: <span className="font-semibold text-green-700">{formatPHP(pendingDepositReceived)}</span></p>
                <p>Current Balance: <span className="font-semibold">{formatPHP(balance)}</span></p>
                {pendingDepositError && <p className="text-amber-700 text-sm">{pendingDepositError}</p>}
                <p className="text-[11px] text-gray-500">
                  Debug: since={depositDebug.kidSince}, eventsSince={depositDebug.kidEventSince}, batch={depositDebug.lastBatchCount}, eventBatch={depositDebug.lastEventBatchCount}, maxId={depositDebug.lastBatchMaxId}, eventMaxId={depositDebug.lastEventMaxId}, last={formatPHP(depositDebug.lastBatchAmount)} @ {depositDebug.lastPollAt || '--:--:--'}
                </p>
              </div>

              <div className="flex items-center gap-2 text-sm text-gray-500 font-inter">
                <span className="inline-block w-2 h-2 rounded-full bg-green-400 animate-pulse"></span>
                {pendingDepositReceived > 0 ? 'Money detected — keep inserting or wait for timer.' : 'Waiting for coins or bills…'}
              </div>

              <button
                type="button"
                onClick={() => setDepositCountdown(0)}
                disabled={pendingDepositReceived <= 0}
                className="w-full rounded-xl bg-green-600 text-white px-4 py-2 font-inter font-semibold disabled:bg-gray-300 disabled:text-gray-500"
              >
                Apply Now
              </button>

              <button
                type="button"
                onClick={() => {
                  if (pendingDepositReceived > 0) {
                    setDepositCountdown(0)
                    return
                  }
                  setPendingDepositReceived(0)
                  setPendingDepositError(null)
                  setKidDepositModalOpen(false)
                  if (kidName) {
                    void releaseDeviceLock(kidName)
                  }
                }}
                className="w-full rounded-xl bg-gray-200 text-gray-800 px-4 py-2 font-inter font-semibold"
              >
                Cancel
              </button>
            </div>
          </div>
        )}

      {withdrawProgress.open && (
        <div className="fixed inset-0 z-[60] bg-black/50 backdrop-blur-sm flex items-center justify-center p-4">
          <div className="w-full max-w-md bg-white rounded-2xl shadow-2xl p-6 space-y-4 text-center">
            <div className="text-7xl">
              {withdrawProgress.phase === 'locking' && '🦊'}
              {withdrawProgress.phase === 'sending' && '🐼'}
              {withdrawProgress.phase === 'dispensing' && '🐰'}
              {withdrawProgress.phase === 'done' && '🎉'}
              {withdrawProgress.phase === 'error' && '😿'}
            </div>

            <h3 className="text-2xl font-sora font-bold text-blue-700">
              {withdrawProgress.phase === 'done' ? 'Withdrawal Complete' : 'Processing Withdrawal'}
            </h3>

            <p className="font-inter text-gray-700">{withdrawProgress.message}</p>

            {withdrawProgress.phase === 'dispensing' && (
              <div className="rounded-xl bg-blue-50 px-4 py-6 font-inter text-blue-800">
                <p className="text-sm mb-3">Dispensing your cash</p>
                <div className="flex items-center justify-center gap-2" aria-label="Dispensing">
                  <span className="block w-3 h-3 rounded-full bg-blue-600 animate-bounce" style={{ animationDelay: '0ms' }} />
                  <span className="block w-3 h-3 rounded-full bg-blue-600 animate-bounce" style={{ animationDelay: '150ms' }} />
                  <span className="block w-3 h-3 rounded-full bg-blue-600 animate-bounce" style={{ animationDelay: '300ms' }} />
                </div>
              </div>
            )}

            {(withdrawProgress.phase === 'locking' || withdrawProgress.phase === 'sending') && (
              <div className="rounded-xl bg-amber-50 px-4 py-3 font-inter text-amber-800">
                <p className="text-sm">Preparing request...</p>
              </div>
            )}

            {withdrawProgress.phase === 'done' && (
              <p className="text-green-700 font-inter font-semibold">Please collect your cash now.</p>
            )}

            {withdrawProgress.phase === 'error' && (
              <button
                type="button"
                onClick={() => setWithdrawProgress((prev) => ({ ...prev, open: false, phase: 'idle' }))}
                className="w-full rounded-xl bg-gray-200 text-gray-800 px-4 py-2 font-inter font-semibold"
              >
                Close
              </button>
            )}
          </div>
        </div>
      )}

      {role === 'parent' && pendingDepositKid && (
        <div className="fixed inset-0 z-50 bg-black/40 backdrop-blur-sm flex items-center justify-center p-4">
          <div className="w-full max-w-lg bg-white rounded-2xl shadow-2xl p-6 space-y-4">
            <h3 className="text-2xl font-sora font-bold text-blue-700">Cash-In for {pendingDepositKid}</h3>
            <p className="font-inter text-gray-700">
              Insert coins or bills into the acceptor. The timer resets with each coin or bill detected.
            </p>

            <div className="flex items-center justify-center">
              <div className={`text-6xl font-sora font-black ${
                depositCountdown <= 5 ? 'text-red-500' : depositCountdown <= 10 ? 'text-amber-500' : 'text-blue-600'
              }`}>{depositCountdown}s</div>
            </div>

            <div className="bg-blue-50 rounded-xl p-4 space-y-2 font-inter text-gray-800">
              <p>Collected so far: <span className="font-semibold text-blue-700">{formatPHP(pendingDepositReceived)}</span></p>
              {pendingDepositError && <p className="text-amber-700 text-sm">{pendingDepositError}</p>}
              <p className="text-[11px] text-gray-500">
                Debug: since={depositDebug.parentSince}, eventsSince={depositDebug.parentEventSince}, batch={depositDebug.lastBatchCount}, eventBatch={depositDebug.lastEventBatchCount}, maxId={depositDebug.lastBatchMaxId}, eventMaxId={depositDebug.lastEventMaxId}, last={formatPHP(depositDebug.lastBatchAmount)} @ {depositDebug.lastPollAt || '--:--:--'}
              </p>
            </div>

            <div className="flex items-center gap-2 text-sm text-gray-500 font-inter">
              <span className="inline-block w-2 h-2 rounded-full bg-blue-400 animate-pulse"></span>
              {pendingDepositReceived > 0 ? 'Money detected — keep inserting or wait for timer.' : 'Waiting for coins or bills…'}
            </div>

            <div className="flex flex-col sm:flex-row gap-2 pt-1">
              <button
                type="button"
                onClick={applyParentReceivedDeposit}
                disabled={pendingDepositReceived <= 0}
                className="flex-1 rounded-xl bg-emerald-600 text-white px-4 py-2 font-inter font-semibold disabled:opacity-50"
              >
                Apply Now
              </button>
              <button
                type="button"
                onClick={() => {
                  if (pendingDepositReceived > 0) {
                    void applyParentReceivedDeposit()
                    return
                  }
                  cancelParentDepositFlow()
                }}
                className="flex-1 rounded-xl bg-gray-200 text-gray-800 px-4 py-2 font-inter font-semibold"
              >
                Cancel
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}
