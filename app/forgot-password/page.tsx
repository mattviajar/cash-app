'use client'

import { useState } from 'react'
import { useRouter } from 'next/navigation'
import Link from 'next/link'

type AccountType = 'parent' | 'kid'

export default function ForgotPasswordPage() {
  const router = useRouter()
  const [accountType, setAccountType] = useState<AccountType>('parent')
  const [username, setUsername] = useState('')
  const [securityAnswer, setSecurityAnswer] = useState('')
  const [securityQuestion, setSecurityQuestion] = useState('')
  const [petVerified, setPetVerified] = useState(false)
  const [newPassword, setNewPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [showNewPassword, setShowNewPassword] = useState(false)
  const [showConfirmPassword, setShowConfirmPassword] = useState(false)

  const handleVerifyPet = (e: React.FormEvent) => {
    e.preventDefault()

    const normalizedUsername = username.trim().toLowerCase()

    if (!normalizedUsername) {
      alert('Please enter your username.')
      return
    }

    if (!securityAnswer.trim()) {
      alert('Please answer your security question.')
      return
    }

    if ((accountType as AccountType) === 'parent') {
      const parentRaw = localStorage.getItem('cash_parent_account')
      if (!parentRaw) {
        alert('No parent account found. Please create an account first.')
        router.push('/create-account')
        return
      }

      const parent = JSON.parse(parentRaw)
      if ((parent.username ?? '').toLowerCase() !== normalizedUsername) {
        alert('Parent username not found.')
        return
      }

      // Load the security question for display
      setSecurityQuestion(parent.securityQuestion ?? "Unknown question")
      
      const storedAnswer = (parent.securityAnswer ?? '').toLowerCase()
      if (storedAnswer !== securityAnswer.trim().toLowerCase()) {
        alert('❌ Incorrect answer. Please try again.')
        return
      }

      setPetVerified(true)
      return
    }

    // Kid account verification
    const validKidAccounts: string[] = JSON.parse(localStorage.getItem('cash_valid_kid_accounts') || '[]')
    const matchedKidUsername = validKidAccounts.find((account) => account.toLowerCase() === normalizedUsername)

    if (!matchedKidUsername) {
      alert('Kid account not found. Ask your parent to create your account first.')
      return
    }

    // Load the security question for display
    const storedQuestion = localStorage.getItem(`cash_kid_sec_question_${matchedKidUsername}`) ?? "Unknown question"
    setSecurityQuestion(storedQuestion)

    const storedAnswer = (localStorage.getItem(`cash_kid_sec_answer_${matchedKidUsername}`) ?? '').toLowerCase()
    if (storedAnswer !== securityAnswer.trim().toLowerCase()) {
      alert('❌ Incorrect answer. Please try again.')
      return
    }

    setPetVerified(true)
  }

  const handleResetPassword = (e: React.FormEvent) => {
    e.preventDefault()

    const normalizedUsername = username.trim().toLowerCase()

    if (newPassword.length < 6) {
      alert('Password must be at least 6 characters long.')
      return
    }

    if (newPassword !== confirmPassword) {
      alert('Passwords do not match.')
      return
    }

    if (accountType === 'parent') {
      const parentRaw = localStorage.getItem('cash_parent_account')
      if (!parentRaw) {
        alert('No parent account found. Please create an account first.')
        router.push('/create-account')
        return
      }

      const parent = JSON.parse(parentRaw)
      const updatedParent = {
        ...parent,
        password: newPassword,
      }
      localStorage.setItem('cash_parent_account', JSON.stringify(updatedParent))
      alert('✅ Parent password updated successfully!')
      router.push('/login')
      return
    }

    const validKidAccounts: string[] = JSON.parse(localStorage.getItem('cash_valid_kid_accounts') || '[]')
    const matchedKidUsername = validKidAccounts.find((account) => account.toLowerCase() === normalizedUsername)

    if (!matchedKidUsername) {
      alert('Kid account not found. Ask your parent to create your account first.')
      return
    }

    localStorage.setItem(`cash_kid_pwd_${matchedKidUsername}`, newPassword)
    alert(`✅ Password updated successfully for ${matchedKidUsername}!`)
    router.push('/login')
  }

  return (
    <div className="min-h-screen relative overflow-hidden">
      <div className="absolute inset-0 bg-gradient-to-br from-blue-500 via-teal-400 to-cyan-300">
        <div className="absolute top-20 left-10 w-72 h-72 bg-blue-400 rounded-full mix-blend-multiply filter blur-xl opacity-60 animate-float"></div>
        <div className="absolute top-40 right-20 w-96 h-96 bg-cyan-300 rounded-full mix-blend-multiply filter blur-xl opacity-60 animate-float" style={{ animationDelay: '0.4s' }}></div>
        <div className="absolute -bottom-8 left-1/3 w-80 h-80 bg-teal-400 rounded-full mix-blend-multiply filter blur-xl opacity-60 animate-float" style={{ animationDelay: '0.8s' }}></div>
      </div>

      <div className="relative z-10 min-h-screen flex flex-col items-center justify-center px-4 py-12">
        <Link
          href="/login"
          className="absolute top-8 left-8 text-white/90 hover:text-white font-inter font-semibold flex items-center gap-2 transition-colors duration-300"
        >
          <span className="text-2xl">←</span>
          <span>Back to Login</span>
        </Link>

        <div className="text-center mb-8">
          <h1 className="text-6xl md:text-7xl font-sora font-black mb-2 tracking-tight text-white drop-shadow-lg">
            C.A.S.H.
          </h1>
          <div className="h-1 w-24 mx-auto bg-gradient-to-r from-blue-300 via-teal-300 to-cyan-200 rounded-full"></div>
        </div>

        <div className="glass-card max-w-md w-full space-y-6">
          <div className="text-center">
            <h2 className="text-3xl font-sora font-bold text-transparent bg-clip-text bg-gradient-to-r from-blue-600 to-teal-500">
              Reset Password 🔐
            </h2>
            <p className="text-gray-600 text-sm font-inter mt-2">
              Usernames are not case-sensitive
            </p>
          </div>

          <div className="flex gap-3 p-1 bg-white/60 rounded-full">
            <button
              type="button"
              onClick={() => {
                setAccountType('parent')
                setPetVerified(false)
                setUsername('')
                setSecurityAnswer('')
                setSecurityQuestion('')
                setNewPassword('')
                setConfirmPassword('')
              }}
              className={`flex-1 py-3 px-6 rounded-full font-sora font-semibold transition-all duration-300 ${
                (accountType as AccountType) === 'parent'
                  ? 'bg-gradient-to-r from-blue-600 to-teal-500 text-white shadow-lg scale-105'
                  : 'text-gray-600 hover:text-gray-800'
              }`}
            >
              👨‍👩‍👧‍👦 Parent
            </button>
            <button
              type="button"
              onClick={() => {
                setAccountType('kid')
                setPetVerified(false)
                setUsername('')
                setSecurityAnswer('')
                setSecurityQuestion('')
                setNewPassword('')
                setConfirmPassword('')
              }}
              className={`flex-1 py-3 px-6 rounded-full font-sora font-semibold transition-all duration-300 ${
                (accountType as AccountType) === 'parent'
                  ? 'bg-gradient-to-r from-blue-600 to-teal-500 text-white shadow-lg scale-105'
                  : 'text-gray-600 hover:text-gray-800'
              }`}
            >
              🧒 Kid
            </button>
          </div>

          <form onSubmit={petVerified ? handleResetPassword : handleVerifyPet} className="space-y-4">
            {!petVerified ? (
              <>
                <div>
                  <label htmlFor="username" className="block text-sm font-inter font-semibold text-gray-700 mb-2">
                    Username
                  </label>
                  <input
                    id="username"
                    type="text"
                    value={username}
                    onChange={(e) => setUsername(e.target.value)}
                    className="w-full px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none transition-all duration-300 font-inter bg-white/80"
                    placeholder="Enter your username"
                    required
                  />
                </div>

                <div>
                  <label htmlFor="securityAnswer" className="block text-sm font-inter font-semibold text-gray-700 mb-2">
                    🔐 Security Question
                  </label>
                  <p className="text-gray-700 font-inter mb-2 p-3 bg-blue-50 rounded-lg text-sm">
                    {securityQuestion || "Enter your username first to see your security question"}
                  </p>
                  <input
                    id="securityAnswer"
                    type="text"
                    value={securityAnswer}
                    onChange={(e) => setSecurityAnswer(e.target.value)}
                    className="w-full px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none transition-all duration-300 font-inter bg-white/80"
                    placeholder="Your answer (case-insensitive)"
                    required
                  />
                  <p className="text-xs text-gray-500 font-inter mt-1">💡 Answer your security question to verify your identity (case-insensitive)</p>
                </div>

                <button
                  type="submit"
                  className="btn-primary w-full text-xl py-4 group relative overflow-hidden mt-6"
                >
                  <span className="relative z-10 flex items-center justify-center gap-2 font-sora font-semibold">
                    Verify Identity
                    <span className="text-2xl group-hover:scale-110 transition-transform duration-300">✅</span>
                  </span>
                  <div className="absolute inset-0 bg-gradient-to-r from-blue-600 to-teal-500 opacity-0 group-hover:opacity-100 transition-opacity duration-300"></div>
                </button>
              </>
            ) : (
              <>
                <div className="bg-green-50 border-2 border-green-200 rounded-xl p-4 mb-4">
                  <p className="text-sm text-green-800 font-inter">
                    ✅ <span className="font-semibold">Identity verified!</span> Now create your new password.
                  </p>
                </div>

                <div>
                  <label htmlFor="newPassword" className="block text-sm font-inter font-semibold text-gray-700 mb-2">
                    New Password
                  </label>
                  <div className="relative">
                    <input
                      id="newPassword"
                      type={showNewPassword ? 'text' : 'password'}
                      value={newPassword}
                      onChange={(e) => setNewPassword(e.target.value)}
                      className="w-full px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none transition-all duration-300 font-inter bg-white/80 pr-12"
                      placeholder="Enter a new password"
                      minLength={6}
                      required
                    />
                    <button
                      type="button"
                      onClick={() => setShowNewPassword(!showNewPassword)}
                      className="absolute right-4 top-1/2 -translate-y-1/2 text-gray-500 hover:text-gray-700 transition-colors duration-200"
                      aria-label={showNewPassword ? 'Hide password' : 'Show password'}
                    >
                      {showNewPassword ? '👁️' : '👁️‍🗨️'}
                    </button>
                  </div>
                </div>

                <div>
                  <label htmlFor="confirmPassword" className="block text-sm font-inter font-semibold text-gray-700 mb-2">
                    Confirm New Password
                  </label>
                  <div className="relative">
                    <input
                      id="confirmPassword"
                      type={showConfirmPassword ? 'text' : 'password'}
                      value={confirmPassword}
                      onChange={(e) => setConfirmPassword(e.target.value)}
                      className="w-full px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none transition-all duration-300 font-inter bg-white/80 pr-12"
                      placeholder="Confirm your new password"
                      minLength={6}
                      required
                    />
                    <button
                      type="button"
                      onClick={() => setShowConfirmPassword(!showConfirmPassword)}
                      className="absolute right-4 top-1/2 -translate-y-1/2 text-gray-500 hover:text-gray-700 transition-colors duration-200"
                      aria-label={showConfirmPassword ? 'Hide password' : 'Show password'}
                    >
                      {showConfirmPassword ? '👁️' : '👁️‍🗨️'}
                    </button>
                  </div>
                </div>

                <button
                  type="submit"
                  className="btn-primary w-full text-xl py-4 group relative overflow-hidden mt-6"
                >
                  <span className="relative z-10 flex items-center justify-center gap-2 font-sora font-semibold">
                    Reset Password
                    <span className="text-2xl group-hover:scale-110 transition-transform duration-300">🔄</span>
                  </span>
                  <div className="absolute inset-0 bg-gradient-to-r from-blue-600 to-teal-500 opacity-0 group-hover:opacity-100 transition-opacity duration-300"></div>
                </button>

                <button
                  type="button"
                  onClick={() => {
                    setPetVerified(false)
                    setSecurityAnswer('')
                    setSecurityQuestion('')
                    setNewPassword('')
                    setConfirmPassword('')
                  }}
                  className="w-full text-sm font-inter text-gray-600 hover:text-gray-800 py-2"
                >
                  ← Back to Verify Identity
                </button>
              </>
            )}
          </form>

          <div className="text-center pt-4 border-t border-teal-200">
            <p className="text-sm font-inter text-gray-600">
              Remembered your password?{' '}
              <Link
                href="/login"
                className="font-semibold text-blue-600 hover:text-blue-700 transition-colors duration-200"
              >
                Login here
              </Link>
            </p>
          </div>
        </div>
      </div>
    </div>
  )
}
