'use client'

import { useState } from 'react'
import { useRouter } from 'next/navigation'
import Link from 'next/link'

export default function LoginPage() {
  const router = useRouter()
  const [showPassword, setShowPassword] = useState(false)
  const [userType, setUserType] = useState<'kid' | 'parent'>('kid')
  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const [rememberMe, setRememberMe] = useState(false)

  const handleLogin = async (e: React.FormEvent) => {
    e.preventDefault()
    const normalizedUsername = username.trim().toLowerCase()

    const res = await fetch('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        username: normalizedUsername,
        password,
        role: userType,
      }),
    })

    if (!res.ok) {
      const data = await res.json().catch(() => ({ error: 'Login failed' }))
      alert(`❌ ${data.error ?? 'Login failed'}`)
      return
    }

    const data = await res.json() as { account: { username: string } }
    sessionStorage.setItem('cash_username', data.account.username)
    
    // Login successful
    sessionStorage.setItem('cash_role', userType)
    router.push('/dashboard')
  }

  return (
    <div className="min-h-screen relative overflow-hidden">
      {/* Animated Background - Same as Welcome Page */}
      <div className="absolute inset-0 bg-gradient-to-br from-blue-500 via-teal-400 to-cyan-300">
        {/* Floating Circles */}
        <div className="absolute top-20 left-10 w-72 h-72 bg-blue-400 rounded-full mix-blend-multiply filter blur-xl opacity-60 animate-float"></div>
        <div className="absolute top-40 right-20 w-96 h-96 bg-cyan-300 rounded-full mix-blend-multiply filter blur-xl opacity-60 animate-float" style={{ animationDelay: '0.4s' }}></div>
        <div className="absolute -bottom-8 left-1/3 w-80 h-80 bg-teal-400 rounded-full mix-blend-multiply filter blur-xl opacity-60 animate-float" style={{ animationDelay: '0.8s' }}></div>
        
        {/* Floating Emojis */}
        <div className="absolute top-1/4 left-1/4 text-6xl animate-bounce-slow" style={{ animationDelay: '0.2s' }}>💰</div>
        <div className="absolute top-1/3 right-1/4 text-5xl animate-bounce-slow" style={{ animationDelay: '0.5s' }}>🪙</div>
        <div className="absolute bottom-1/4 left-1/2 text-4xl animate-bounce-slow" style={{ animationDelay: '0.8s' }}>💵</div>
        <div className="absolute bottom-1/3 right-1/3 text-5xl animate-bounce-slow">🎯</div>
        <div className="absolute top-2/3 left-1/3 text-4xl animate-bounce-slow" style={{ animationDelay: '0.35s' }}>🏦</div>
      </div>

      {/* Content */}
      <div className="relative z-10 min-h-screen flex flex-col items-center justify-center px-4">
        {/* Back to Welcome */}
        <Link 
          href="/"
          className="absolute top-8 left-8 text-white/90 hover:text-white font-inter font-semibold flex items-center gap-2 transition-colors duration-300"
        >
          <span className="text-2xl">←</span>
          <span>Back</span>
        </Link>

        {/* Title */}
        <div className="text-center mb-8">
          <h1 className="text-6xl md:text-7xl font-sora font-black mb-2 tracking-tight text-white drop-shadow-lg">
            C.A.S.H.
          </h1>
          <div className="h-1 w-24 mx-auto bg-gradient-to-r from-blue-300 via-teal-300 to-cyan-200 rounded-full"></div>
        </div>

        {/* Login Card */}
        <div className="glass-card max-w-md w-full space-y-6">
          <div className="text-center">
            <h2 className="text-3xl font-sora font-bold text-transparent bg-clip-text bg-gradient-to-r from-blue-600 to-teal-500">
              Welcome Back! 👋
            </h2>
          </div>

          {/* User Type Selector */}
          <div className="flex gap-3 p-1 bg-white/60 rounded-full">
            <button
              type="button"
              onClick={() => setUserType('kid')}
              className={`flex-1 py-3 px-6 rounded-full font-sora font-semibold transition-all duration-300 ${
                userType === 'kid'
                  ? 'bg-gradient-to-r from-blue-600 to-teal-500 text-white shadow-lg scale-105'
                  : 'text-gray-600 hover:text-gray-800'
              }`}
            >
              🧒 Kid
            </button>
            <button
              type="button"
              onClick={() => setUserType('parent')}
              className={`flex-1 py-3 px-6 rounded-full font-sora font-semibold transition-all duration-300 ${
                userType === 'parent'
                  ? 'bg-gradient-to-r from-blue-600 to-teal-500 text-white shadow-lg scale-105'
                  : 'text-gray-600 hover:text-gray-800'
              }`}
            >
              👨‍👩‍👧‍👦 Parent
            </button>
          </div>

          {/* Info Box */}
          <div className={`rounded-xl p-4 text-sm font-inter ${
            userType === 'kid' 
              ? 'bg-blue-50 border-2 border-blue-200 text-blue-800' 
              : 'bg-green-50 border-2 border-green-200 text-green-800'
          }`}>
            {userType === 'kid' ? (
              <p>
                <span className="font-semibold">👶 Kid Login:</span> Your parent needs to create your account first. Ask them to set up a username and password for you!
              </p>
            ) : (
              <p>
                <span className="font-semibold">👨‍👩‍👧‍👦 Parent Login:</span> Log in to manage settings, create kid accounts, and approve withdrawals.
              </p>
            )}
          </div>

          {/* Login Form */}
          <form onSubmit={handleLogin} className="space-y-5">
            {/* Username Field */}
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

            {/* Password Field */}
            <div>
              <label htmlFor="password" className="block text-sm font-inter font-semibold text-gray-700 mb-2">
                Password
              </label>
              <div className="relative">
                <input
                  id="password"
                  type={showPassword ? 'text' : 'password'}
                  value={password}
                  onChange={(e) => setPassword(e.target.value)}
                  className="w-full px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none transition-all duration-300 font-inter bg-white/80 pr-12"
                  placeholder="Enter your password"
                  required
                />
                <button
                  type="button"
                  onClick={() => setShowPassword(!showPassword)}
                  className="absolute right-4 top-1/2 -translate-y-1/2 text-gray-500 hover:text-gray-700 transition-colors duration-200"
                  aria-label={showPassword ? 'Hide password' : 'Show password'}
                >
                  {showPassword ? '👁️' : '👁️‍🗨️'}
                </button>
              </div>
            </div>

            {/* Remember Me */}
            <div className="flex items-center">
              <input
                id="remember"
                type="checkbox"
                checked={rememberMe}
                onChange={(e) => setRememberMe(e.target.checked)}
                className="w-4 h-4 text-blue-600 border-gray-300 rounded focus:ring-blue-500 cursor-pointer"
              />
              <label htmlFor="remember" className="ml-2 text-sm font-inter font-medium text-gray-700 cursor-pointer">
                Remember me
              </label>
            </div>

            {/* Login Button */}
            <button
              type="submit"
              className="btn-primary w-full text-xl py-4 group relative overflow-hidden"
            >
              <span className="relative z-10 flex items-center justify-center gap-2 font-sora font-semibold">
                Login
                <span className="text-2xl group-hover:translate-x-1 transition-transform duration-300">→</span>
              </span>
              <div className="absolute inset-0 bg-gradient-to-r from-blue-600 to-teal-500 opacity-0 group-hover:opacity-100 transition-opacity duration-300"></div>
            </button>
          </form>

          {/* Links */}
          <div className="space-y-3 pt-4 border-t border-teal-200">
            <div className="flex justify-between items-center">
              <Link 
                href="/forgot-password"
                className="text-sm font-inter font-medium text-blue-600 hover:text-blue-700 transition-colors duration-200"
              >
                Forgot Password?
              </Link>
              <Link 
                href="/create-account"
                className="text-sm font-inter font-medium text-blue-600 hover:text-blue-700 transition-colors duration-200"
              >
                Create Account
              </Link>
            </div>
            <Link 
              href="/faq"
              className="block text-center text-sm font-inter font-medium text-teal-600 hover:text-teal-700 transition-colors duration-200"
            >
              ❓ Need Help? Check our FAQ
            </Link>
          </div>
        </div>

        {/* Footer */}
        <p className="mt-8 text-white/80 text-center text-sm font-inter">
          Start your savings journey today! 🚀
        </p>
      </div>
    </div>
  )
}
