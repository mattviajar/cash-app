'use client'

import { useEffect, useMemo, useState } from 'react'
import { useRouter } from 'next/navigation'
import Link from 'next/link'

type Role = 'kid' | 'parent'
type MenuKey = 'dashboard' | 'goals' | 'transactions' | 'statistics' | 'settings' | 'profile'

type WithdrawalRecord = {
  id: number
  child: string
  amount: number
  note: string
  when: string
}

type PendingWithdrawal = {
  id: number
  child: string
  amount: number
  note: string
  createdAt: string
}

type Goal = {
  id: number
  name: string
  saved: number
  target: number
}

const menuItems: Array<{ key: MenuKey; label: string; icon: string }> = [
  { key: 'dashboard', label: 'Dashboard', icon: '🏠' },
  { key: 'goals', label: 'Goals', icon: '🎯' },
  { key: 'transactions', label: 'Transactions', icon: '🧾' },
  { key: 'statistics', label: 'Statistics', icon: '📊' },
  { key: 'settings', label: 'Settings', icon: '⚙️' },
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
  history: 'cash_withdrawal_history',
  kidNotifications: 'cash_kid_notifications',
  kidShowBalance: 'cash_kid_show_balance',
  kidRequireNote: 'cash_kid_require_note',
  kidDailyLimit: 'cash_kid_daily_limit',
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

function safeParse<T>(raw: string | null, fallback: T): T {
  if (!raw) return fallback
  try {
    return JSON.parse(raw) as T
  } catch {
    return fallback
  }
}

export default function DashboardPage() {
  const router = useRouter()
  const [role, setRole] = useState<Role>('kid')
  const [activeMenu, setActiveMenu] = useState<MenuKey>('dashboard')
  const [balance, setBalance] = useState(124.75)
  const [withdrawAmount, setWithdrawAmount] = useState('')
  const [withdrawNote, setWithdrawNote] = useState('')
  const [instantWithdrawals, setInstantWithdrawals] = useState(false)
  const [pendingWithdrawals, setPendingWithdrawals] = useState<PendingWithdrawal[]>([])
  const [history, setHistory] = useState<WithdrawalRecord[]>(defaultHistory)
  const [kidGoalsByAccount, setKidGoalsByAccount] = useState<Record<string, Goal[]>>({})
  const [kidNotifications, setKidNotifications] = useState(true)
  const [kidShowBalance, setKidShowBalance] = useState(true)
  const [kidRequireNote, setKidRequireNote] = useState(false)
  const [kidDailyWithdrawLimit, setKidDailyWithdrawLimit] = useState(25)
  const [parentSpendingAlerts, setParentSpendingAlerts] = useState(true)
  const [parentAutoApproveLimit, setParentAutoApproveLimit] = useState(0)
  const [newGoalName, setNewGoalName] = useState('')
  const [newGoalTarget, setNewGoalTarget] = useState('')
  const [kidCharacter, setKidCharacter] = useState('astronaut')
  const [kidName, setKidName] = useState('')
  const [validKidAccounts, setValidKidAccounts] = useState<string[]>([])
  const [kidBalances, setKidBalances] = useState<{ [key: string]: number }>({})
  const [newKidUsername, setNewKidUsername] = useState('')
  const [newKidPassword, setNewKidPassword] = useState('')
  const [newKidSecurityQuestion, setNewKidSecurityQuestion] = useState("What's your favorite pet?")
  const [newKidSecurityAnswer, setNewKidSecurityAnswer] = useState('')
  const [newKidCustomQuestion, setNewKidCustomQuestion] = useState('')
  const [isHydrated, setIsHydrated] = useState(false)

  const kidGoals = useMemo(() => {
    if (!kidName) return initialKidGoals
    return kidGoalsByAccount[kidName] ?? initialKidGoals
  }, [kidGoalsByAccount, kidName])

  useEffect(() => {
    const storedRole = sessionStorage.getItem(STORAGE_KEYS.role)
    const role = storedRole === 'parent' ? 'parent' : 'kid'
    setRole(role)

    const loadedKidBalances = safeParse<Record<string, number>>(
      localStorage.getItem(STORAGE_KEYS.kidBalances),
      {}
    )
    setKidBalances(loadedKidBalances)

    const loadedKidAccounts = safeParse<string[]>(
      localStorage.getItem(STORAGE_KEYS.validKidAccounts),
      []
    )
    setValidKidAccounts(loadedKidAccounts)

    const loadedGoalsByAccount = safeParse<Record<string, Goal[]>>(
      localStorage.getItem(STORAGE_KEYS.kidGoalsByAccount),
      {}
    )

    // If kid, set their name from username
    if (role === 'kid') {
      const username = sessionStorage.getItem('cash_username')
      if (username) {
        setKidName(username)
        setBalance(loadedKidBalances[username] ?? 0)

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
    }

    setKidGoalsByAccount(loadedGoalsByAccount)

    setInstantWithdrawals(localStorage.getItem(STORAGE_KEYS.instant) === 'true')
    setPendingWithdrawals(safeParse<PendingWithdrawal[]>(localStorage.getItem(STORAGE_KEYS.pending), []))
    setHistory(safeParse<WithdrawalRecord[]>(localStorage.getItem(STORAGE_KEYS.history), defaultHistory))

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

    const storedDailyLimit = localStorage.getItem(STORAGE_KEYS.kidDailyLimit)
    if (storedDailyLimit) {
      const parsedLimit = Number(storedDailyLimit)
      if (Number.isFinite(parsedLimit) && parsedLimit > 0) {
        setKidDailyWithdrawLimit(parsedLimit)
      }
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

    setIsHydrated(true)

  }, [])

  useEffect(() => {
    if (!isHydrated) return
    localStorage.setItem(STORAGE_KEYS.instant, String(instantWithdrawals))
    localStorage.setItem(STORAGE_KEYS.pending, JSON.stringify(pendingWithdrawals))
    localStorage.setItem(STORAGE_KEYS.history, JSON.stringify(history))
    localStorage.setItem(STORAGE_KEYS.kidNotifications, String(kidNotifications))
    localStorage.setItem(STORAGE_KEYS.kidShowBalance, String(kidShowBalance))
    localStorage.setItem(STORAGE_KEYS.kidRequireNote, String(kidRequireNote))
    localStorage.setItem(STORAGE_KEYS.kidDailyLimit, String(kidDailyWithdrawLimit))
    localStorage.setItem(STORAGE_KEYS.parentAlerts, String(parentSpendingAlerts))
    localStorage.setItem(STORAGE_KEYS.parentAutoApproveLimit, String(parentAutoApproveLimit))
    localStorage.setItem(STORAGE_KEYS.kidGoalsByAccount, JSON.stringify(kidGoalsByAccount))
    localStorage.setItem(STORAGE_KEYS.kidCharacter, kidCharacter)
    localStorage.setItem(STORAGE_KEYS.validKidAccounts, JSON.stringify(validKidAccounts))
    localStorage.setItem(STORAGE_KEYS.kidBalances, JSON.stringify(kidBalances))
  }, [
    isHydrated,
    instantWithdrawals,
    pendingWithdrawals,
    history,
    kidNotifications,
    kidShowBalance,
    kidRequireNote,
    kidDailyWithdrawLimit,
    parentSpendingAlerts,
    parentAutoApproveLimit,
    kidGoalsByAccount,
    kidCharacter,
    validKidAccounts,
    kidBalances,
  ])

  useEffect(() => {
    if (!isHydrated) return
    if (role !== 'kid' || !kidName) return
    setKidBalances((prev) => {
      const current = prev[kidName] ?? 0
      if (current === balance) return prev
      return { ...prev, [kidName]: balance }
    })
  }, [isHydrated, role, kidName, balance])

  useEffect(() => {
    if (role === 'kid' && activeMenu === 'settings') {
      setActiveMenu('dashboard')
    }
  }, [role, activeMenu])

  const visibleMenuItems = role === 'kid' ? menuItems.filter((item) => item.key !== 'settings') : menuItems

  const canWithdraw = useMemo(() => {
    const amount = Number(withdrawAmount)
    const noteOk = !kidRequireNote || withdrawNote.trim().length >= 3
    return (
      Number.isFinite(amount) &&
      amount > 0 &&
      amount <= balance &&
      amount <= kidDailyWithdrawLimit &&
      noteOk
    )
  }, [withdrawAmount, balance, kidDailyWithdrawLimit, kidRequireNote, withdrawNote])

  const kidHistory = history.filter((entry) => entry.child === kidName)
  const pendingForKid = pendingWithdrawals.filter((entry) => entry.child === kidName)
  const totalGoalSaved = kidGoals.reduce((sum, goal) => sum + goal.saved, 0)
  const totalGoalTarget = kidGoals.reduce((sum, goal) => sum + goal.target, 0)
  const totalGoalProgress = totalGoalTarget > 0 ? Math.round((totalGoalSaved / totalGoalTarget) * 100) : 0
  const topGoal = (kidGoals.length > 0 ? [...kidGoals].sort((a, b) => b.saved / b.target - a.saved / a.target)[0] : null)
  const totalKidSpent = kidHistory.reduce((sum, item) => sum + item.amount, 0)
  const kidAverageWithdrawal = kidHistory.length > 0 ? totalKidSpent / kidHistory.length : 0
  const kidLargestWithdrawal = kidHistory.length > 0 ? Math.max(...kidHistory.map((item) => item.amount)) : 0
  const kidApprovalRate = kidHistory.length + pendingForKid.length > 0
    ? Math.round((kidHistory.length / (kidHistory.length + pendingForKid.length)) * 100)
    : 100
  const selectedCharacter = characterOptions.find((option) => option.id === kidCharacter) ?? characterOptions[0]
  
  const kidSpendingByNote = Object.entries(
    kidHistory.reduce<Record<string, number>>((acc, item) => {
      const key = item.note.trim() || 'Other'
      acc[key] = (acc[key] ?? 0) + item.amount
      return acc
    }, {})
  ).slice(0, 5)
  const parentPending = pendingWithdrawals
  const parentChildren = validKidAccounts.map((username, index) => ({
    id: index + 1,
    name: username,
    balance: kidBalances[username] || 0,
    withdrawalsThisWeek: history.filter((entry) => entry.child === username).length,
  }))
  const parentSpendingByChild = parentChildren.map((child) => ({
    name: child.name,
    amount: history.filter((entry) => entry.child === child.name).reduce((sum, entry) => sum + entry.amount, 0),
  }))
  const parentGoals = validKidAccounts.flatMap((username) =>
    (kidGoalsByAccount[username] ?? []).map((goal) => ({
      ...goal,
      child: username,
      key: `${username}-${goal.id}`,
    }))
  )

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

  const resetWithdrawForm = () => {
    setWithdrawAmount('')
    setWithdrawNote('')
  }

  const handleWithdraw = (e: React.FormEvent) => {
    e.preventDefault()
    if (!canWithdraw) return

    const amount = Number(withdrawAmount)
    const note = withdrawNote.trim() || 'Withdrawal'

    if (instantWithdrawals || (parentAutoApproveLimit > 0 && amount <= parentAutoApproveLimit)) {
      setBalance((prev) => Number((prev - amount).toFixed(2)))
      setKidBalances((prev) => ({
        ...prev,
        [kidName]: Number(((prev[kidName] ?? 0) - amount).toFixed(2)),
      }))
      setHistory((prev) => [
        {
          id: Date.now(),
          child: kidName,
          amount,
          note,
          when: instantWithdrawals ? 'Just now' : 'Auto-approved now',
        },
        ...prev,
      ])
    } else {
      setPendingWithdrawals((prev) => [
        {
          id: Date.now(),
          child: kidName,
          amount,
          note,
          createdAt: 'Just now',
        },
        ...prev,
      ])
    }

    resetWithdrawForm()
  }

  const approvePending = (id: number) => {
    const request = pendingWithdrawals.find((item) => item.id === id)
    if (!request) return
    const childBalance = kidBalances[request.child] ?? 0
    if (request.amount > childBalance) return

    setKidBalances((prev) => ({
      ...prev,
      [request.child]: Number(((prev[request.child] ?? 0) - request.amount).toFixed(2)),
    }))
    setHistory((prev) => [
      {
        id: Date.now(),
        child: request.child,
        amount: request.amount,
        note: request.note,
        when: 'Approved now',
      },
      ...prev,
    ])
    setPendingWithdrawals((prev) => prev.filter((item) => item.id !== id))
  }

  const declinePending = (id: number) => {
    setPendingWithdrawals((prev) => prev.filter((item) => item.id !== id))
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

  const kidDashboardView = (
    <section className="space-y-6">
      {kidName && (
        <div className="glass-card bg-gradient-to-r from-blue-500/10 to-teal-500/10">
          <h2 className="text-3xl font-sora font-bold text-transparent bg-clip-text bg-gradient-to-r from-blue-600 to-teal-500">
            Welcome back, {kidName}! 👋
          </h2>
        </div>
      )}
      
      <div className="glass-card">
        <p className="text-gray-700 font-inter font-semibold">Current Balance</p>
        <h2 className="text-5xl font-sora font-black text-transparent bg-clip-text bg-gradient-to-r from-blue-600 to-teal-500 mt-2">
          {kidShowBalance ? `$${balance.toFixed(2)}` : '•••••'}
        </h2>
        <p className="text-gray-600 font-inter mt-2">Deposits are handled by the machine automatically.</p>
      </div>

      <div className="grid md:grid-cols-2 lg:grid-cols-4 gap-4">
        <button
          type="button"
          onClick={() => setActiveMenu('goals')}
          className="glass-card text-left hover:scale-[1.01] transition-transform"
        >
          <p className="text-sm text-gray-600 font-inter">Goals Snapshot</p>
          <p className="text-3xl font-sora font-black text-blue-700 mt-2">{totalGoalProgress}%</p>
          <p className="text-sm text-gray-700 font-inter mt-1">Overall progress</p>
        </button>

        <button
          type="button"
          onClick={() => setActiveMenu('transactions')}
          className="glass-card text-left hover:scale-[1.01] transition-transform"
        >
          <p className="text-sm text-gray-600 font-inter">Transactions</p>
          <p className="text-3xl font-sora font-black text-blue-700 mt-2">{kidHistory.length}</p>
          <p className="text-sm text-gray-700 font-inter mt-1">{pendingForKid.length} pending</p>
        </button>

        <button
          type="button"
          onClick={() => setActiveMenu('statistics')}
          className="glass-card text-left hover:scale-[1.01] transition-transform"
        >
          <p className="text-sm text-gray-600 font-inter">Spent Total</p>
          <p className="text-3xl font-sora font-black text-blue-700 mt-2">${totalKidSpent.toFixed(2)}</p>
          <p className="text-sm text-gray-700 font-inter mt-1">All withdrawals</p>
        </button>

        <button
          type="button"
          onClick={() => setActiveMenu('profile')}
          className="glass-card text-left hover:scale-[1.01] transition-transform"
        >
          <p className="text-sm text-gray-600 font-inter">Character Profile</p>
          <p className="text-xl font-sora font-black text-blue-700 mt-2">
            {selectedCharacter.emoji} {selectedCharacter.title}
          </p>
          <p className="text-sm text-gray-700 font-inter mt-1">Customize your avatar</p>
        </button>
      </div>

      <div className="glass-card">
        <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Withdraw</h3>
        <form onSubmit={handleWithdraw} className="grid gap-4 md:grid-cols-3">
          <input
            type="number"
            min="0"
            step="0.01"
            value={withdrawAmount}
            onChange={(e) => setWithdrawAmount(e.target.value)}
            placeholder="Amount"
            className="px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter bg-white/80"
          />
          <input
            type="text"
            value={withdrawNote}
            onChange={(e) => setWithdrawNote(e.target.value)}
            placeholder="Reason (optional)"
            className="px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter bg-white/80"
          />
          <button
            type="submit"
            disabled={!canWithdraw}
            className="btn-primary disabled:opacity-50 disabled:cursor-not-allowed"
          >
            {instantWithdrawals ? 'Withdraw Now' : 'Request Withdrawal'}
          </button>
        </form>
        {!instantWithdrawals && (
          <p className="text-sm text-amber-700 font-inter mt-2">Parent approval is required before this withdrawal is processed.</p>
        )}
        {!canWithdraw && withdrawAmount !== '' && (
          <p className="text-sm text-red-600 font-inter mt-2">
            Enter a valid amount not greater than your balance or daily limit, and add a note if required.
          </p>
        )}
      </div>

      <div className="grid lg:grid-cols-2 gap-6">
        <div className="glass-card">
          <h3 className="text-2xl font-sora font-bold text-blue-700 mb-3">Goal Highlight</h3>
          <p className="text-gray-700 font-inter font-semibold">{topGoal ? topGoal.name : 'No goals yet'}</p>
          <p className="text-sm text-gray-600 font-inter mt-1">Best progress so far</p>
          <div className="w-full h-3 bg-white/60 rounded-full overflow-hidden mt-3">
            <div
              className="h-full bg-gradient-to-r from-blue-600 to-teal-500"
              style={{ width: `${topGoal ? Math.round((topGoal.saved / topGoal.target) * 100) : 0}%` }}
            ></div>
          </div>
          <p className="text-sm text-gray-700 font-inter mt-2">{topGoal ? `$${topGoal.saved} / $${topGoal.target}` : 'Add your first goal in Goals'}</p>
        </div>

        <div className="glass-card">
          <h3 className="text-2xl font-sora font-bold text-blue-700 mb-3">Quick View</h3>
          <div className="space-y-2 font-inter text-gray-700">
            <p>• Latest transaction: {kidHistory[0] ? `${kidHistory[0].note} (-$${kidHistory[0].amount.toFixed(2)})` : 'No transactions yet'}</p>
            <p>• Pending approvals: {pendingForKid.length}</p>
            <p>• Reminder notifications: {kidNotifications ? 'On' : 'Off'}</p>
            <p>• Balance visibility: {kidShowBalance ? 'Visible' : 'Hidden'}</p>
          </div>
        </div>
      </div>
    </section>
  )

  const kidGoalsView = (
    <section className="space-y-6">
      <div className="grid md:grid-cols-3 gap-4">
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Total Saved</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">${totalGoalSaved}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Goal Target</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">${totalGoalTarget}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Completion</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">{totalGoalProgress}%</p>
        </div>
      </div>

      <div className="glass-card">
        <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Add New Goal</h3>
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
        <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Savings Goals</h3>
        <div className="space-y-5">
          {kidGoals.map((goal) => {
            const percent = Math.min(100, Math.round((goal.saved / goal.target) * 100))
            const remaining = goal.target - goal.saved
            return (
              <div key={goal.id} className="bg-white/70 rounded-xl p-4">
                <div className="flex justify-between text-sm font-inter font-semibold text-gray-700 mb-1">
                  <span>{goal.name}</span>
                  <span>${goal.saved} / ${goal.target}</span>
                </div>
                <div className="w-full h-3 bg-white/60 rounded-full overflow-hidden mt-2">
                  <div className="h-full bg-gradient-to-r from-blue-600 to-teal-500" style={{ width: `${percent}%` }}></div>
                </div>
                <div className="mt-3 flex items-center justify-between text-xs font-inter text-gray-600">
                  <span>{percent}% complete</span>
                  <span>${remaining} to go</span>
                </div>
              </div>
            )
          })}
        </div>
      </div>

      <div className="glass-card">
        <h4 className="text-xl font-sora font-bold text-blue-700 mb-3">Goal Completion Graph</h4>
        <div className="space-y-3">
          {kidGoals.map((goal) => {
            const percent = Math.min(100, Math.round((goal.saved / goal.target) * 100))
            return (
              <div key={goal.id}>
                <div className="flex justify-between text-xs font-inter text-gray-700 mb-1">
                  <span>{goal.name}</span>
                  <span>{percent}%</span>
                </div>
                <div className="w-full h-4 bg-white/60 rounded-lg overflow-hidden">
                  <div className="h-full bg-gradient-to-r from-blue-600 to-teal-500" style={{ width: `${percent}%` }}></div>
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
        <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Recent Withdrawals</h3>
        <div className="space-y-3">
          {kidHistory.slice(0, 6).map((item) => (
            <div key={item.id} className="bg-white/70 rounded-xl px-4 py-3 flex items-center justify-between">
              <div>
                <p className="font-inter font-semibold text-gray-800">{item.note}</p>
                <p className="text-xs text-gray-600 font-inter">{item.when}</p>
              </div>
              <p className="font-sora font-bold text-red-600">-${item.amount.toFixed(2)}</p>
            </div>
          ))}
        </div>
      </div>

      <div className="glass-card">
        <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Pending Requests</h3>
        <div className="space-y-3">
          {pendingWithdrawals.length === 0 ? (
            <p className="font-inter text-gray-700">No pending requests.</p>
          ) : (
            pendingWithdrawals.map((item) => (
              <div key={item.id} className="bg-white/70 rounded-xl px-4 py-3">
                <p className="font-inter font-semibold text-gray-800">${item.amount.toFixed(2)} • {item.note}</p>
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
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">${totalKidSpent.toFixed(2)}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Average Withdrawal</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">${kidAverageWithdrawal.toFixed(2)}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Largest Withdrawal</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">${kidLargestWithdrawal.toFixed(2)}</p>
        </div>
      </div>

      <div className="grid lg:grid-cols-2 gap-6">
        <div className="glass-card">
          <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Spending by Category</h3>
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
                      <span>${amount.toFixed(2)} ({percentage}%)</span>
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
          <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Goal Progress Graph</h3>
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
          <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Goal Progress Stats</h3>
          <div className="space-y-3 font-inter text-gray-700">
            <p>• Goals active: <span className="font-semibold">{kidGoals.length}</span></p>
            <p>• Combined progress: <span className="font-semibold">{totalGoalProgress}%</span></p>
            <p>• Top goal: <span className="font-semibold">{topGoal ? topGoal.name : 'No goal yet'}</span></p>
            <p>• Total saved toward goals: <span className="font-semibold">${totalGoalSaved}</span></p>
          </div>
        </div>

        <div className="glass-card">
          <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Approval & Activity</h3>
          <div className="space-y-3 font-inter text-gray-700">
            <p>• Pending requests: <span className="font-semibold">{pendingForKid.length}</span></p>
            <p>• Approval completion rate: <span className="font-semibold">{kidApprovalRate}%</span></p>
            <p>• Daily withdraw limit: <span className="font-semibold">${kidDailyWithdrawLimit}</span></p>
            <p>• Current policy: <span className="font-semibold">{instantWithdrawals ? 'Instant mode' : 'Parent approval mode'}</span></p>
          </div>
        </div>
      </div>
    </section>
  )

  const kidSettingsView = (
    <section className="space-y-6">
      <div className="glass-card">
        <h3 className="text-2xl font-sora font-bold text-blue-700 mb-3">Settings Locked</h3>
        <p className="font-inter text-gray-700">
          Only parents can access and change settings. If you need to update limits or permissions,
          ask a parent to open the Parent Settings page.
        </p>
      </div>
      <div className="glass-card">
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
      <div className="glass-card">
        <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Profile</h3>
        <div className="bg-white/70 rounded-xl p-4 font-inter">
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

      <div className="glass-card">
        <h4 className="text-xl font-sora font-bold text-blue-700 mb-3">Choose Your Character</h4>
        <div className="grid grid-cols-2 md:grid-cols-3 gap-3">
          {characterOptions.map((option) => (
            <button
              key={option.id}
              type="button"
              onClick={() => setKidCharacter(option.id)}
              className={`rounded-xl p-3 text-left transition-all border-2 ${
                kidCharacter === option.id
                  ? 'border-blue-500 bg-white shadow-md'
                  : 'border-transparent bg-white/70 hover:bg-white'
              }`}
            >
              <p className="text-3xl">{option.emoji}</p>
              <p className="font-sora font-semibold text-blue-700 mt-1">{option.title}</p>
            </button>
          ))}
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
    <section className="space-y-6">
      {validKidAccounts.length === 0 ? (
        <div className="glass-card text-center py-8">
          <div className="text-6xl mb-4">👶</div>
          <h3 className="text-2xl font-sora font-bold text-gray-700 mb-2">No Kid Accounts Yet</h3>
          <p className="text-gray-600 font-inter mb-4">
            Get started by creating kid accounts in Settings
          </p>
          <button
            type="button"
            onClick={() => setActiveMenu('settings')}
            className="btn-primary inline-block"
          >
            Go to Settings
          </button>
        </div>
      ) : (
        <>
          <div className="glass-card">
            <h2 className="text-2xl font-sora font-bold text-blue-700 mb-4">Quick Actions</h2>
            <div className="grid md:grid-cols-2 gap-4">
              {validKidAccounts.map((username) => (
                <div key={username} className="bg-white/70 rounded-xl p-4">
                  <p className="font-inter font-semibold text-gray-800 mb-2">Add Money to {username}</p>
                  <div className="flex gap-2">
                    <button
                      type="button"
                      onClick={() => {
                        const amount = prompt(`How much would you like to add to ${username}'s account?`)
                        if (amount) {
                          const parsedAmount = parseFloat(amount)
                          if (parsedAmount > 0 && !isNaN(parsedAmount)) {
                            setKidBalances(prev => ({
                              ...prev,
                              [username]: (prev[username] || 0) + parsedAmount
                            }))
                            alert(`✅ Added $${parsedAmount.toFixed(2)} to ${username}'s account!`)
                          } else {
                            alert('❌ Invalid amount')
                          }
                        }
                      }}
                      className="flex-1 px-4 py-2 rounded-lg bg-gradient-to-r from-green-600 to-green-500 text-white font-inter font-semibold hover:shadow-lg transition-all"
                    >
                      💰 Deposit
                    </button>
                  </div>
                </div>
              ))}
            </div>
          </div>

          <div className="grid lg:grid-cols-2 gap-6">
            <div className="glass-card">
              <h2 className="text-2xl font-sora font-bold text-blue-700 mb-4">All Kids Balances</h2>
              <div className="space-y-3">
                {parentChildren.map((child) => (
                  <div key={child.id} className="bg-white/70 rounded-xl px-4 py-3 flex items-center justify-between">
                    <div>
                      <p className="font-inter font-semibold text-gray-800">{child.name}</p>
                      <p className="text-xs text-gray-600 font-inter">{child.withdrawalsThisWeek} withdrawals logged</p>
                    </div>
                    <p className="font-sora font-bold text-blue-700">${child.balance.toFixed(2)}</p>
                  </div>
                ))}
              </div>
            </div>

            <div className="glass-card">
              <h2 className="text-2xl font-sora font-bold text-blue-700 mb-4">Alerts</h2>
              <div className="space-y-3">
                {parentAlerts.map((alert, index) => (
                  <div key={index} className="bg-white/70 rounded-xl px-4 py-3">
                    <p className="font-inter font-semibold text-gray-800">⚠️ {alert}</p>
                  </div>
                ))}
              </div>
            </div>
          </div>
        </>
      )}
    </section>
  )

  const parentGoalsView = (
    <section className="glass-card">
      <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Kids Goal Progress</h3>
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
    </section>
  )

  const parentTransactionsView = (
    <section className="grid lg:grid-cols-2 gap-6">
      <div className="glass-card">
        <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Pending Authorization</h3>
        <div className="space-y-3">
          {parentPending.length === 0 ? (
            <p className="font-inter text-gray-700">No pending requests.</p>
          ) : (
            parentPending.map((item) => (
              <div key={item.id} className="bg-white/70 rounded-xl px-4 py-3">
                <p className="font-inter font-semibold text-gray-800">{item.child} • ${item.amount.toFixed(2)}</p>
                <p className="text-xs text-gray-600 font-inter mb-3">{item.note} • {item.createdAt}</p>
                <div className="flex gap-2">
                  <button
                    type="button"
                    onClick={() => approvePending(item.id)}
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
        <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Withdrawal History</h3>
        <div className="space-y-3">
          {history.slice(0, 8).map((entry) => (
            <div key={entry.id} className="bg-white/70 rounded-xl px-4 py-3 flex items-center justify-between">
              <div>
                <p className="font-inter font-semibold text-gray-800">{entry.child} • {entry.note}</p>
                <p className="text-xs text-gray-600 font-inter">{entry.when}</p>
              </div>
              <p className="font-sora font-bold text-red-600">-${entry.amount.toFixed(2)}</p>
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
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">{history.length}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Total Amount</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">
            ${history.reduce((sum, item) => sum + item.amount, 0).toFixed(2)}
          </p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Pending Requests</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">{parentPending.length}</p>
        </div>
        <div className="glass-card text-center">
          <p className="text-gray-600 font-inter">Average Withdrawal</p>
          <p className="text-4xl font-sora font-black text-blue-700 mt-2">
            ${history.length > 0 ? (history.reduce((sum, item) => sum + item.amount, 0) / history.length).toFixed(2) : '0.00'}
          </p>
        </div>
      </div>

      <div className="grid lg:grid-cols-2 gap-6">
        <div className="glass-card">
          <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Withdrawals by Child (Graph)</h3>
          <div className="space-y-3">
            {parentSpendingByChild.map((row) => {
              const maxAmount = Math.max(...parentSpendingByChild.map((item) => item.amount), 1)
              const widthPercent = Math.round((row.amount / maxAmount) * 100)
              return (
                <div key={row.name}>
                  <div className="flex justify-between text-xs font-inter text-gray-700 mb-1">
                    <span>{row.name}</span>
                    <span>${row.amount.toFixed(2)}</span>
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
          <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Approval Flow (Graph)</h3>
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
          <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Policy Snapshot</h3>
          <div className="space-y-2 font-inter text-gray-700">
            <p>• Instant withdrawals: <span className="font-semibold">{instantWithdrawals ? 'Enabled' : 'Disabled'}</span></p>
            <p>• Kid daily limit: <span className="font-semibold">${kidDailyWithdrawLimit}</span></p>
            <p>• Auto-approve limit: <span className="font-semibold">${parentAutoApproveLimit.toFixed(2)}</span></p>
            <p>• Spending alerts: <span className="font-semibold">{parentSpendingAlerts ? 'On' : 'Off'}</span></p>
          </div>
        </div>

        <div className="glass-card">
          <h3 className="text-2xl font-sora font-bold text-blue-700 mb-4">Child Breakdown</h3>
          <div className="space-y-2 font-inter text-gray-700">
            {parentChildren.map((child) => (
              <p key={child.id}>
                • {child.name}: <span className="font-semibold">${child.balance.toFixed(2)}</span> balance, {child.withdrawalsThisWeek} withdrawals
              </p>
            ))}
          </div>
        </div>
      </div>
    </section>
  )

  const parentSettingsView = (
    <section className="space-y-6">
      <div className="glass-card space-y-5">
        <h3 className="text-2xl font-sora font-bold text-blue-700">Parent Settings</h3>
        <div className="bg-white/70 rounded-xl p-4 flex items-center justify-between gap-4">
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
                ? 'bg-gradient-to-r from-blue-600 to-teal-500 text-white'
                : 'bg-white text-gray-700 border border-gray-300'
            }`}
          >
            {instantWithdrawals ? 'Enabled' : 'Disabled'}
          </button>
        </div>

        <div className="bg-white/70 rounded-xl p-4 flex items-center justify-between gap-4">
          <div>
            <p className="font-inter font-semibold text-gray-800">Spending alerts</p>
            <p className="text-sm text-gray-600 font-inter">Show alerts for kid withdrawal activity and thresholds.</p>
          </div>
          <button
            type="button"
            onClick={() => setParentSpendingAlerts((prev) => !prev)}
            className={`px-4 py-2 rounded-lg font-sora font-semibold transition-all ${
              parentSpendingAlerts
                ? 'bg-gradient-to-r from-blue-600 to-teal-500 text-white'
                : 'bg-white text-gray-700 border border-gray-300'
            }`}
          >
            {parentSpendingAlerts ? 'On' : 'Off'}
          </button>
        </div>

        <div className="bg-white/70 rounded-xl p-4">
          <p className="font-inter font-semibold text-gray-800 mb-2">Kid daily withdraw limit ($)</p>
          <div className="flex items-center gap-3 mb-4">
            <input
              type="number"
              min="1"
              step="1"
              value={kidDailyWithdrawLimit}
              onChange={(e) => setKidDailyWithdrawLimit(Math.max(1, Number(e.target.value) || 1))}
              className="w-32 px-3 py-2 rounded-lg border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter"
            />
            <p className="text-sm text-gray-600 font-inter">Maximum request amount allowed for kids.</p>
          </div>

          <p className="font-inter font-semibold text-gray-800 mb-2">Auto-approve limit ($)</p>
          <div className="flex items-center gap-3">
            <input
              type="number"
              min="0"
              step="0.01"
              value={parentAutoApproveLimit}
              onChange={(e) => setParentAutoApproveLimit(Math.max(0, Number(e.target.value) || 0))}
              className="w-32 px-3 py-2 rounded-lg border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter"
            />
            <p className="text-sm text-gray-600 font-inter">
              Requests at or below this amount are auto-approved when instant mode is off.
            </p>
          </div>
        </div>

        <div className="border-t border-blue-200 pt-6">
          <h4 className="text-lg font-sora font-bold text-blue-700 mb-3">👶 Create Kid Accounts</h4>
          <div className="bg-blue-50 border-2 border-blue-200 rounded-xl p-4 mb-4">
            <p className="text-sm text-blue-800 font-inter mb-4">
              Create accounts for your children. They'll use these credentials to log in and access their savings dashboard.
            </p>
            <div className="space-y-3">
              <input
                type="text"
                value={newKidUsername}
                onChange={(e) => setNewKidUsername(e.target.value)}
                placeholder="Kid username (e.g., Sarah, Tommy)"
                className="w-full px-4 py-2 rounded-lg border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter"
              />
              <input
                type="password"
                value={newKidPassword}
                onChange={(e) => setNewKidPassword(e.target.value)}
                placeholder="Create a password for them"
                className="w-full px-4 py-2 rounded-lg border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter"
              />
              <input
                type="text"
                value={newKidSecurityQuestion === 'Custom question' ? newKidCustomQuestion : ''}
                onChange={(e) => setNewKidCustomQuestion(e.target.value)}
                placeholder="Your custom security question"
                className="w-full px-4 py-2 rounded-lg border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter"
              />
              <select
                value={newKidSecurityQuestion}
                onChange={(e) => setNewKidSecurityQuestion(e.target.value)}
                className="w-full px-4 py-2 rounded-lg border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter"
              >
                <option value="What's your favorite pet?">What's your favorite pet?</option>
                <option value="What's your favorite color?">What's your favorite color?</option>
                <option value="What's your favorite food?">What's your favorite food?</option>
                <option value="What city were you born in?">What city were you born in?</option>
                <option value="What's the name of your best friend?">What's the name of your best friend?</option>
                <option value="Custom question">Custom question</option>
              </select>
              {newKidSecurityQuestion === 'Custom question' && (
                <input
                  type="text"
                  value={newKidCustomQuestion}
                  onChange={(e) => setNewKidCustomQuestion(e.target.value)}
                  placeholder="Your custom security question"
                  className="w-full px-4 py-2 rounded-lg border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter"
                />
              )}
              <input
                type="text"
                value={newKidSecurityAnswer}
                onChange={(e) => setNewKidSecurityAnswer(e.target.value)}
                placeholder="🔐 Answer to security question"
                className="w-full px-4 py-2 rounded-lg border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none font-inter"
              />
              <button
                type="button"
                onClick={() => {
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
                  // Store the password
                  localStorage.setItem(`cash_kid_pwd_${username}`, newKidPassword)
                  // Store the security question and answer
                  localStorage.setItem(`cash_kid_sec_question_${username}`, finalQuestion)
                  localStorage.setItem(`cash_kid_sec_answer_${username}`, newKidSecurityAnswer.trim().toLowerCase())
                  // Update the accounts list
                  setValidKidAccounts(prev => [...prev, username])
                  // Initialize balance to 0
                  setKidBalances(prev => ({ ...prev, [username]: 0 }))
                  // Clear the form
                  setNewKidUsername('')
                  setNewKidPassword('')
                  setNewKidSecurityQuestion("What's your favorite pet?")
                  setNewKidSecurityAnswer('')
                  setNewKidCustomQuestion('')
                  alert(`✅ Kid account "${username}" created successfully!`)
                }}
                className="w-full bg-blue-600 hover:bg-blue-700 text-white font-sora font-semibold py-2 px-4 rounded-lg transition-all"
              >
                ➕ Create Kid Account
              </button>
            </div>
          </div>

          {validKidAccounts.length > 0 && (
            <div className="bg-white/70 rounded-xl p-4">
              <p className="font-inter font-semibold text-gray-800 mb-3">Active Kid Accounts:</p>
              <div className="space-y-2">
                {validKidAccounts.map((username) => (
                  <div key={username} className="flex items-center justify-between bg-white rounded-lg p-3 border border-gray-200">
                    <div className="flex-1">
                      <p className="font-inter font-bold text-gray-800">👶 {username}</p>
                      <p className="text-sm text-gray-600 font-inter">Balance: ${(kidBalances[username] || 0).toFixed(2)}</p>
                    </div>
                    <button
                      type="button"
                      onClick={() => {
                        if (window.confirm(`Delete kid account "${username}"? This cannot be undone.`)) {
                          localStorage.removeItem(`cash_kid_pwd_${username}`)
                          setValidKidAccounts(prev => prev.filter(u => u !== username))
                          setKidBalances(prev => {
                            const newBalances = { ...prev }
                            delete newBalances[username]
                            return newBalances
                          })
                          alert(`✅ Kid account "${username}" has been deleted.`)
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

        <div className="border-t border-red-200 pt-6">
          <h4 className="text-lg font-sora font-bold text-red-600 mb-3">⚠️ Danger Zone</h4>
          <div className="bg-red-50 border-2 border-red-200 rounded-xl p-4">
            <p className="font-inter font-semibold text-gray-800 mb-3">Clear all accounts and data</p>
            <p className="text-sm text-gray-700 font-inter mb-4">
              This will permanently delete all accounts, balances, goals, transactions, and app data. This action cannot be undone.
            </p>
            <button
              type="button"
              onClick={() => {
                if (window.confirm('Are you absolutely sure? This will delete ALL data including all accounts, balances, goals, and transactions. This cannot be undone.')) {
                  const existingKidAccounts = JSON.parse(localStorage.getItem(STORAGE_KEYS.validKidAccounts) || '[]')
                  // Clear all localStorage keys from STORAGE_KEYS
                  Object.values(STORAGE_KEYS).forEach(key => {
                    localStorage.removeItem(key)
                  })
                  // Clear parent account
                  localStorage.removeItem('cash_parent_account')
                  // Clear all kid passwords
                  existingKidAccounts.forEach((username: string) => {
                    localStorage.removeItem(`cash_kid_pwd_${username}`)
                  })
                  // Clear sessionStorage
                  sessionStorage.clear()
                  // Redirect to home page
                  router.push('/')
                }
              }}
              className="w-full bg-red-600 hover:bg-red-700 text-white font-sora font-semibold py-3 px-4 rounded-lg transition-all transform hover:scale-105 active:scale-95"
            >
              🗑️ Clear All Data
            </button>
          </div>
        </div>
      </div>
    </section>
  )

  const parentProfileView = (
    <section className="glass-card space-y-4">
      <h3 className="text-2xl font-sora font-bold text-blue-700">Profile</h3>
      <div className="bg-white/70 rounded-xl p-4 font-inter">
        <p><span className="font-semibold">Name:</span> Parent Account</p>
        <p><span className="font-semibold">Role:</span> Parent</p>
        <p><span className="font-semibold">Permissions:</span> View balances, approve withdrawals, manage settings</p>
      </div>
    </section>
  )

  const kidContent: Record<MenuKey, JSX.Element> = {
    dashboard: kidDashboardView,
    goals: kidGoalsView,
    transactions: kidTransactionsView,
    statistics: kidStatisticsView,
    settings: kidSettingsView,
    profile: kidProfileView,
  }

  const parentContent: Record<MenuKey, JSX.Element> = {
    dashboard: parentDashboardView,
    goals: parentGoalsView,
    transactions: parentTransactionsView,
    statistics: parentStatisticsView,
    settings: parentSettingsView,
    profile: parentProfileView,
  }

  return (
    <div className="min-h-screen relative overflow-hidden bg-gradient-to-br from-blue-500 via-teal-400 to-cyan-300">
      <div className="absolute inset-0">
        <div className="absolute top-20 left-10 w-72 h-72 bg-blue-400 rounded-full mix-blend-multiply filter blur-xl opacity-50 animate-float"></div>
        <div className="absolute top-40 right-20 w-96 h-96 bg-cyan-300 rounded-full mix-blend-multiply filter blur-xl opacity-50 animate-float" style={{ animationDelay: '0.4s' }}></div>
      </div>

      <div className="relative z-10 min-h-screen flex">
        <aside className="w-72 bg-white/25 backdrop-blur-xl border-r border-white/30 p-5 hidden md:flex md:flex-col">
          <div className="mb-6">
            <h1 className="text-3xl font-sora font-black text-white drop-shadow-lg">C.A.S.H.</h1>
            <p className="text-white/85 font-inter text-sm">Learn • Save • Achieve</p>
          </div>

          <div className="p-3 bg-white/60 rounded-xl mb-6">
            <p className="text-sm font-inter font-semibold text-gray-700">
              View: <span className="font-sora text-blue-700">{role === 'kid' ? 'Kid' : 'Parent'}</span>
            </p>
          </div>

          <nav className="space-y-2">
            {visibleMenuItems.map((item) => (
              <button
                key={item.key}
                type="button"
                onClick={() => setActiveMenu(item.key)}
                className={`w-full flex items-center gap-3 px-4 py-3 rounded-xl text-left font-inter font-semibold transition-all ${
                  activeMenu === item.key
                    ? 'bg-white text-blue-700 shadow-md'
                    : 'text-white hover:bg-white/20'
                }`}
              >
                <span>{item.icon}</span>
                <span>{item.label}</span>
              </button>
            ))}
          </nav>

          <div className="mt-auto">
            <Link
              href="/login"
              className="block w-full text-center bg-white text-blue-700 font-sora font-semibold py-3 rounded-xl hover:bg-blue-50 transition-colors"
            >
              Logout
            </Link>
          </div>
        </aside>

        <main className="flex-1 p-4 md:p-8">
          <div className="md:hidden mb-4 space-y-3">
            <div className="flex items-center justify-between">
              <h1 className="text-3xl font-sora font-black text-white drop-shadow-lg">C.A.S.H.</h1>
              <span className="bg-white/80 text-blue-700 px-4 py-2 rounded-xl font-sora font-semibold">
                {role === 'kid' ? 'Kid View' : 'Parent View'}
              </span>
            </div>
            <div className="flex gap-2 overflow-x-auto pb-1">
              {visibleMenuItems.map((item) => (
                <button
                  key={item.key}
                  type="button"
                  onClick={() => setActiveMenu(item.key)}
                  className={`whitespace-nowrap px-3 py-2 rounded-lg font-inter font-semibold ${
                    activeMenu === item.key ? 'bg-white text-blue-700' : 'bg-white/40 text-white'
                  }`}
                >
                  {item.icon} {item.label}
                </button>
              ))}
            </div>
          </div>

          {role === 'kid' ? kidContent[activeMenu] : parentContent[activeMenu]}
        </main>
      </div>

      {/* Puppy widget removed */}
    </div>
  )
}
