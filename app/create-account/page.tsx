'use client'

import { useState } from 'react'
import { useRouter } from 'next/navigation'
import Link from 'next/link'

const SECURITY_QUESTIONS = [
  "What's your favorite pet?",
  "What's your favorite color?",
  "What's your favorite food?",
  "What city were you born in?",
  "What's the name of your best friend?",
  "Custom question"
]

export default function CreateAccountPage() {
  const router = useRouter()
  const [showPassword, setShowPassword] = useState(false)
  const [showConfirmPassword, setShowConfirmPassword] = useState(false)
  const [userType, setUserType] = useState<'parent'>('parent')
  const [formData, setFormData] = useState({
    username: '',
    email: '',
    password: '',
    confirmPassword: '',
    securityQuestion: "What's your favorite pet?",
    securityAnswer: '',
    customQuestion: ''
  })

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault()
    const normalizedUsername = formData.username.trim().toLowerCase()
    
    // Validation
    if (!normalizedUsername) {
      alert('Username is required!')
      return
    }

    if (formData.password !== formData.confirmPassword) {
      alert('Passwords do not match!')
      return
    }

    if (!formData.securityAnswer.trim()) {
      alert('Please answer the security question!')
      return
    }

    let finalQuestion = formData.securityQuestion === 'Custom question' ? formData.customQuestion : formData.securityQuestion
    if (!finalQuestion.trim()) {
      alert('Please enter your custom security question!')
      return
    }

    finalQuestion = finalQuestion.trim()

    const res = await fetch('/api/auth/register-parent', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        username: normalizedUsername,
        password: formData.password,
        securityQuestion: finalQuestion,
        securityAnswer: formData.securityAnswer.trim().toLowerCase(),
      }),
    })

    if (!res.ok) {
      const data = await res.json().catch(() => ({ error: 'Failed to create parent account' }))
      alert(`❌ ${data.error ?? 'Failed to create parent account'}`)
      return
    }

    alert('✅ Parent account created successfully in database! You can now login.')
    router.push('/login')
  }

  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    setFormData({
      ...formData,
      [e.target.name]: e.target.value
    })
  }

  return (
    <div className="min-h-screen relative overflow-hidden">
      {/* Animated Background */}
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
      <div className="relative z-10 min-h-screen flex flex-col items-center justify-center px-4 py-12">
        {/* Back to Login */}
        <Link 
          href="/login"
          className="absolute top-8 left-8 text-white/90 hover:text-white font-inter font-semibold flex items-center gap-2 transition-colors duration-300"
        >
          <span className="text-2xl">←</span>
          <span>Back to Login</span>
        </Link>

        {/* Title */}
        <div className="text-center mb-8">
          <h1 className="text-6xl md:text-7xl font-sora font-black mb-2 tracking-tight text-white drop-shadow-lg">
            C.A.S.H.
          </h1>
          <div className="h-1 w-24 mx-auto bg-gradient-to-r from-blue-300 via-teal-300 to-cyan-200 rounded-full"></div>
        </div>

        {/* Create Account Card */}
        <div className="glass-card max-w-md w-full space-y-6">
          <div className="text-center">
            <h2 className="text-3xl font-sora font-bold text-transparent bg-clip-text bg-gradient-to-r from-blue-600 to-teal-500">
              Parent Account 👨‍👩‍👧‍👦
            </h2>
            <p className="text-gray-600 text-sm font-inter mt-2">
              Set up parental controls and create kid accounts
            </p>
          </div>

          {/* Info Box */}
          <div className="bg-blue-50 border-2 border-blue-200 rounded-xl p-4">
            <p className="text-sm text-blue-800 font-inter">
              <span className="font-semibold">👶 Are you a kid?</span> Ask your parent to create an account and set up your kid account from their dashboard. Kids cannot create their own accounts.
            </p>
          </div>

          {/* Create Account Form */}
          <form onSubmit={handleSubmit} className="space-y-4">
            {/* Username Field */}
            <div>
              <label htmlFor="username" className="block text-sm font-inter font-semibold text-gray-700 mb-2">
                Username
              </label>
              <input
                id="username"
                name="username"
                type="text"
                value={formData.username}
                onChange={handleChange}
                className="w-full px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none transition-all duration-300 font-inter bg-white/80"
                placeholder="Choose a username"
                required
              />
            </div>

            {/* Email Field */}
            <div>
              <label htmlFor="email" className="block text-sm font-inter font-semibold text-gray-700 mb-2">
                Email Address
              </label>
              <input
                id="email"
                name="email"
                type="email"
                value={formData.email}
                onChange={handleChange}
                className="w-full px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none transition-all duration-300 font-inter bg-white/80"
                placeholder="your.email@example.com"
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
                  name="password"
                  type={showPassword ? 'text' : 'password'}
                  value={formData.password}
                  onChange={handleChange}
                  className="w-full px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none transition-all duration-300 font-inter bg-white/80 pr-12"
                  placeholder="Create a password"
                  required
                  minLength={6}
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

            {/* Confirm Password Field */}
            <div>
              <label htmlFor="confirmPassword" className="block text-sm font-inter font-semibold text-gray-700 mb-2">
                Confirm Password
              </label>
              <div className="relative">
                <input
                  id="confirmPassword"
                  name="confirmPassword"
                  type={showConfirmPassword ? 'text' : 'password'}
                  value={formData.confirmPassword}
                  onChange={handleChange}
                  className="w-full px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none transition-all duration-300 font-inter bg-white/80 pr-12"
                  placeholder="Confirm your password"
                  required
                  minLength={6}
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

            {/* Security Question Selection */}
            <div>
              <label htmlFor="securityQuestion" className="block text-sm font-inter font-semibold text-gray-700 mb-2">
                🔐 Security Question
              </label>
              <select
                id="securityQuestion"
                name="securityQuestion"
                value={formData.securityQuestion}
                onChange={(e) => setFormData({ ...formData, securityQuestion: e.target.value })}
                className="w-full px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none transition-all duration-300 font-inter bg-white/80"
                required
              >
                {SECURITY_QUESTIONS.map((q) => (
                  <option key={q} value={q}>
                    {q}
                  </option>
                ))}
              </select>
            </div>

            {/* Custom Question Field */}
            {formData.securityQuestion === 'Custom question' && (
              <div>
                <label htmlFor="customQuestion" className="block text-sm font-inter font-semibold text-gray-700 mb-2">
                  Your Custom Question
                </label>
                <input
                  id="customQuestion"
                  name="customQuestion"
                  type="text"
                  value={formData.customQuestion}
                  onChange={handleChange}
                  className="w-full px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none transition-all duration-300 font-inter bg-white/80"
                  placeholder="e.g., What's my dog's name?"
                  required
                />
              </div>
            )}

            {/* Security Answer Field */}
            <div>
              <label htmlFor="securityAnswer" className="block text-sm font-inter font-semibold text-gray-700 mb-2">
                Answer to Your Security Question
              </label>
              <input
                id="securityAnswer"
                name="securityAnswer"
                type="text"
                value={formData.securityAnswer}
                onChange={handleChange}
                className="w-full px-4 py-3 rounded-xl border-2 border-blue-200 focus:border-blue-500 focus:ring-2 focus:ring-blue-200 outline-none transition-all duration-300 font-inter bg-white/80"
                placeholder="Your answer (case-insensitive)"
                required
              />
              <p className="text-xs text-gray-500 font-inter mt-1">💡 You'll need to remember this if you forget your password</p>
            </div>

            {/* Create Account Button */}
            <button
              type="submit"
              className="btn-primary w-full text-xl py-4 group relative overflow-hidden mt-6"
            >
              <span className="relative z-10 flex items-center justify-center gap-2 font-sora font-semibold">
                Create Account
                <span className="text-2xl group-hover:scale-110 transition-transform duration-300">✨</span>
              </span>
              <div className="absolute inset-0 bg-gradient-to-r from-blue-600 to-teal-500 opacity-0 group-hover:opacity-100 transition-opacity duration-300"></div>
            </button>
          </form>

          {/* Link to Login */}
          <div className="text-center pt-4 border-t border-teal-200">
            <p className="text-sm font-inter text-gray-600">
              Already have an account?{' '}
              <Link 
                href="/login"
                className="font-semibold text-blue-600 hover:text-blue-700 transition-colors duration-200"
              >
                Login here
              </Link>
            </p>
          </div>
        </div>

        {/* Footer */}
        <p className="mt-8 text-white/80 text-center text-sm font-inter">
          Your savings journey starts here! 🚀
        </p>
      </div>
    </div>
  )
}
