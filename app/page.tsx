'use client'

import { useRouter } from 'next/navigation'
import Link from 'next/link'

export default function WelcomePage() {
  const router = useRouter()

  return (
    <div className="min-h-screen relative overflow-hidden">
      {/* Animated Background */}
      <div className="absolute inset-0 bg-gradient-to-br from-blue-500 via-teal-400 to-cyan-300">
        {/* Floating Circles */}
        <div className="absolute top-20 left-10 w-72 h-72 bg-blue-400 rounded-full mix-blend-multiply filter blur-xl opacity-60 animate-float"></div>
        <div className="absolute top-40 right-20 w-96 h-96 bg-cyan-300 rounded-full mix-blend-multiply filter blur-xl opacity-60 animate-float" style={{ animationDelay: '0.4s' }}></div>
        <div className="absolute -bottom-8 left-1/3 w-80 h-80 bg-teal-400 rounded-full mix-blend-multiply filter blur-xl opacity-60 animate-float" style={{ animationDelay: '0.8s' }}></div>
        
        {/* Floating Coins Animation */}
        <div className="absolute top-1/4 left-1/4 text-6xl animate-bounce-slow" style={{ animationDelay: '0.2s' }}>💰</div>
        <div className="absolute top-1/3 right-1/4 text-5xl animate-bounce-slow" style={{ animationDelay: '0.5s' }}>🪙</div>
        <div className="absolute bottom-1/4 left-1/2 text-4xl animate-bounce-slow" style={{ animationDelay: '0.8s' }}>💵</div>
        <div className="absolute bottom-1/3 right-1/3 text-5xl animate-bounce-slow">🎯</div>
        <div className="absolute top-2/3 left-1/3 text-4xl animate-bounce-slow" style={{ animationDelay: '0.35s' }}>🏦</div>
      </div>

      {/* Content */}
      <div className="relative z-10 min-h-screen flex flex-col items-center justify-center px-4">
        {/* Title */}
        <div className="text-center mb-12">
          <h1 className="cash-title text-8xl md:text-9xl font-sora font-black mb-2 tracking-tight">
            <span className="cash-shine-text" data-text="C.A.S.H.">C.A.S.H.</span>
            <span className="cash-spark" aria-hidden="true">✦</span>
          </h1>
          <div className="h-1 w-32 mx-auto bg-gradient-to-r from-blue-300 via-teal-300 to-cyan-200 rounded-full mb-6"></div>
          <p className="text-2xl md:text-3xl text-white/95 font-sora font-semibold drop-shadow-lg tracking-wide">
            Learn • Save • Achieve
          </p>
        </div>

        {/* Glass Card Container */}
        <div className="glass-card max-w-md w-full text-center space-y-6">
          <div className="space-y-3">
            <h2 className="text-4xl font-sora font-bold text-transparent bg-clip-text bg-gradient-to-r from-blue-600 to-teal-500">
              Welcome!
            </h2>
            <p className="text-gray-700 text-lg font-inter font-medium leading-relaxed">
              Your adventure to becoming a savings superstar starts here! 🌟
            </p>
          </div>

          {/* Enter Button */}
          <button
            onClick={() => router.push('/login')}
            className="btn-primary w-full text-2xl py-6 group relative overflow-hidden"
          >
            <span className="relative z-10 flex items-center justify-center gap-3 font-sora font-semibold">
              Let's Go! 
              <span className="text-3xl group-hover:translate-x-2 transition-transform duration-300">🚀</span>
            </span>
            <div className="absolute inset-0 bg-gradient-to-r from-blue-600 to-teal-500 opacity-0 group-hover:opacity-100 transition-opacity duration-300"></div>
          </button>

          {/* Fun Stats Preview */}
          <div className="grid grid-cols-3 gap-4 pt-6 border-t border-teal-200">
            <div className="text-center">
              <div className="text-3xl mb-1">🎯</div>
              <div className="text-sm text-gray-700 font-inter font-semibold">Set Goals</div>
            </div>
            <div className="text-center">
              <div className="text-3xl mb-1">📊</div>
              <div className="text-sm text-gray-700 font-inter font-semibold">Track Progress</div>
            </div>
            <div className="text-center">
              <div className="text-3xl mb-1">🏆</div>
              <div className="text-sm text-gray-700 font-inter font-semibold">Earn Rewards</div>
            </div>
          </div>
        </div>

        {/* Footer Text */}
        <p className="mt-12 text-white/90 text-center max-w-2xl px-4 text-lg font-inter font-medium drop-shadow-lg leading-relaxed">
          Parents and kids working together to build healthy money habits! 💪
        </p>

        {/* FAQ Link */}
        <Link 
          href="/faq"
          className="mt-6 text-white/80 hover:text-white font-inter font-medium text-sm transition-colors duration-200 underline"
        >
          ❓ Have questions? Check our FAQ
        </Link>
      </div>
    </div>
  )
}
