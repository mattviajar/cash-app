'use client'

import { useState } from 'react'
import Link from 'next/link'

interface FAQItem {
  id: string
  question: string
  answer: string
  category: 'getting-started' | 'kid-features' | 'parent-features' | 'troubleshooting'
}

export default function FAQPage() {
  const [expandedId, setExpandedId] = useState<string | null>(null)
  const [selectedCategory, setSelectedCategory] = useState<string>('all')

  const faqItems: FAQItem[] = [
    // Getting Started
    {
      id: 'gs-1',
      category: 'getting-started',
      question: '🤔 How do I create an account?',
      answer: 'Click on "Create Account" from the login page. Choose whether you\'re a Kid or Parent, enter your username and password, then click "Create Account". Your account will be saved and you can log in anytime!'
    },
    {
      id: 'gs-2',
      category: 'getting-started',
      question: '🔐 Is my information safe?',
      answer: 'Yes! All your information is stored securely in your device\'s local storage. Your data never leaves your device. Make sure to remember your password and keep your device secure!'
    },
    {
      id: 'gs-3',
      category: 'getting-started',
      question: '👨‍👩‍👧‍👦 What\'s the difference between Kid and Parent accounts?',
      answer: 'Kid accounts allow you to track savings, add goals, and view your progress. Parent accounts let you manage settings, approve withdrawals, set daily limits, and view all kids\' activities and statistics!'
    },
    {
      id: 'gs-4',
      category: 'getting-started',
      question: '🎯 How do I choose my character?',
      answer: 'Go to your Profile (as a Kid). Click on the character area and choose from 6 different characters. Your selection is saved automatically!'
    },

    // Kid Features
    {
      id: 'kid-1',
      category: 'kid-features',
      question: '💰 How do I add money to my account?',
      answer: 'As a kid, you can\'t directly add money yourself. Your parent needs to add deposits for you using the Parent Dashboard. Ask your parent to approve a deposit!'
    },
    {
      id: 'kid-2',
      category: 'kid-features',
      question: '🎯 How do I create a savings goal?',
      answer: 'Go to the Goals page and click "Add a New Goal". Enter a goal name (like "Bicycle" or "Video Game"), set your target amount, and click "Create Goal". Your progress will be tracked automatically!'
    },
    {
      id: 'kid-3',
      category: 'kid-features',
      question: '📊 What do the numbers in Statistics mean?',
      answer: 'Statistics show your spending habits, goal progress, and total savings. See where you\'re spending money and how close you are to reaching your goals. This helps you understand your money better!'
    },
    {
      id: 'kid-4',
      category: 'kid-features',
      question: '💸 How do I withdraw money?',
      answer: 'Click the blue "Withdraw" button on the dashboard. Enter the amount you want to take out, write a note about why you\'re withdrawing, and submit. Your parent will approve or deny it!'
    },
    {
      id: 'kid-5',
      category: 'kid-features',
      question: '✏️ Can I edit my username?',
      answer: 'Yes! Go to your Profile page and click on your name to edit it. Your new username will be saved automatically.'
    },

    // Parent Features
    {
      id: 'parent-1',
      category: 'parent-features',
      question: '👶 How do I add a child to my account?',
      answer: 'Each child needs their own Kid account. Create their account on the login page, then from the Parent Dashboard, you can see all registered kids and manage their settings and withdrawals.'
    },
    {
      id: 'parent-2',
      category: 'parent-features',
      question: '💵 How do I approve a deposit for my child?',
      answer: 'Go to the Parent Dashboard and look at the top section. You\'ll see all pending deposits. Click "Approve" to add the money to their account, or mark it as "Not Approved" if you\'d like to decline it.'
    },
    {
      id: 'parent-3',
      category: 'parent-features',
      question: '🚫 How do I reject a withdrawal?',
      answer: 'In the Parent Dashboard, check the Transactions section. If there\'s a pending withdrawal you want to reject, click "Not Approved" to deny it. The money stays in the child\'s account.'
    },
    {
      id: 'parent-4',
      category: 'parent-features',
      question: '⚙️ What settings can I configure?',
      answer: 'In Parent Settings, you can: enable Instant Mode (auto-approve certain transactions), require withdrawal notes, set daily spending limits, enable price notifications, and toggle auto-approval for deposits.'
    },
    {
      id: 'parent-5',
      category: 'parent-features',
      question: '📈 What do the Parent Statistics show?',
      answer: 'Parent Statistics displays: total withdrawals per child, approval/denial flow, transaction trends, and spending patterns. This helps you monitor and understand your child\'s financial behavior.'
    },
    {
      id: 'parent-6',
      category: 'parent-features',
      question: '🔒 Can I lock my child\'s settings?',
      answer: 'Yes! In Parent Settings, you can enable restrictions so your child cannot access the Settings page. This helps prevent unwanted changes to their account.'
    },

    // Troubleshooting
    {
      id: 'ts-1',
      category: 'troubleshooting',
      question: '❌ I forgot my password. What do I do?',
      answer: 'Regular login doesn\'t have password recovery yet. If you forget your password, you\'ll need to create a new account with a different username. Consider writing your password down somewhere safe!'
    },
    {
      id: 'ts-2',
      category: 'troubleshooting',
      question: '🔄 My data disappeared. Where is it?',
      answer: 'Your data is stored locally on this device. If you clear your browser\'s cache/cookies, your data will be lost. Always back up important information and avoid clearing browser data if you want to keep your account!'
    },
    {
      id: 'ts-3',
      category: 'troubleshooting',
      question: '💾 Will my data be saved if I close the app?',
      answer: 'Yes! All your data is automatically saved to your device\'s storage. When you log back in, everything will be exactly where you left it.'
    },
    {
      id: 'ts-4',
      category: 'troubleshooting',
      question: '⚠️ I can\'t find my kid\'s account transactions.',
      answer: 'Go to the Parent Dashboard and select the specific kid from the top. All their transactions, goals, and statistics will load for that child. Make sure you\'re looking at the correct child!'
    },
    {
      id: 'ts-5',
      category: 'troubleshooting',
      question: '❓ A goal shows 0% progress even though I saved money.',
      answer: 'Progress only increases when you specifically save towards that goal. Make sure your savings are linked to the goal. Check that the goal is still active and you haven\'t reset it.'
    },
    {
      id: 'ts-6',
      category: 'troubleshooting',
      question: '🆘 My math doesn\'t add up. Numbers seem wrong.',
      answer: 'Amount = Balance + All Pending Withdrawals + All Approvals. Try checking: (1) Have you refreshed the page? (2) Do pending transactions exist? (3) Small rounding from currency conversion? If issues persist, try logging out and back in!'
    },
    {
      id: 'ts-7',
      category: 'troubleshooting',
      question: '🖥️ The app doesn\'t work on a different device.',
      answer: 'Because data is stored locally, each device has separate data. If you want the same account on another device, you\'ll need to log in with the same credentials, but data won\'t sync automatically. Consider using one main device!'
    },
    {
      id: 'ts-8',
      category: 'troubleshooting',
      question: '❌ A button isn\'t working or the app seems frozen.',
      answer: 'Try: (1) Refresh the page (F5 or Cmd+R), (2) Clear your browser cache, (3) Log out and log back in, (4) Try a different browser. If it still doesn\'t work, try on a different device!'
    }
  ]

  const categories = [
    { id: 'all', label: 'All Questions', icon: '❓' },
    { id: 'getting-started', label: 'Getting Started', icon: '🚀' },
    { id: 'kid-features', label: 'For Kids', icon: '🧒' },
    { id: 'parent-features', label: 'For Parents', icon: '👨‍👩‍👧‍👦' },
    { id: 'troubleshooting', label: 'Troubleshooting', icon: '🆘' }
  ]

  const filteredFAQs = selectedCategory === 'all' 
    ? faqItems 
    : faqItems.filter(item => item.category === selectedCategory)

  return (
    <div className="min-h-screen relative overflow-hidden">
      {/* Animated Background */}
      <div className="absolute inset-0 bg-gradient-to-br from-blue-500 via-teal-400 to-cyan-300">
        <div className="absolute top-20 left-10 w-72 h-72 bg-blue-400 rounded-full mix-blend-multiply filter blur-xl opacity-60 animate-float"></div>
        <div className="absolute top-40 right-20 w-96 h-96 bg-cyan-300 rounded-full mix-blend-multiply filter blur-xl opacity-60 animate-float" style={{ animationDelay: '0.4s' }}></div>
        <div className="absolute -bottom-8 left-1/3 w-80 h-80 bg-teal-400 rounded-full mix-blend-multiply filter blur-xl opacity-60 animate-float" style={{ animationDelay: '0.8s' }}></div>
      </div>

      {/* Content */}
      <div className="relative z-10 min-h-screen">
        {/* Header */}
        <div className="bg-gradient-to-r from-blue-600/90 to-teal-500/90 backdrop-blur-sm py-8 px-4 sticky top-0 z-20 border-b border-white/10">
          <div className="max-w-4xl mx-auto flex items-center justify-between">
            <div className="flex items-center gap-3">
              <Link 
                href="/"
                className="text-white/90 hover:text-white transition-colors duration-300"
                title="Back to home"
              >
                <span className="text-3xl">←</span>
              </Link>
              <h1 className="text-4xl font-sora font-bold text-white">FAQ</h1>
            </div>
            <span className="text-3xl">❓</span>
          </div>
        </div>

        {/* Main Content */}
        <div className="max-w-4xl mx-auto px-4 py-12">
          {/* Category Filter */}
          <div className="mb-12">
            <h2 className="text-2xl font-sora font-bold text-white mb-6 text-center">What do you need help with?</h2>
            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-5 gap-3">
              {categories.map(cat => (
                <button
                  key={cat.id}
                  onClick={() => setSelectedCategory(cat.id)}
                  className={`py-3 px-4 rounded-xl font-sora font-semibold transition-all duration-300 transform hover:scale-105 ${
                    selectedCategory === cat.id
                      ? 'bg-gradient-to-r from-blue-600 to-teal-500 text-white shadow-lg scale-105'
                      : 'bg-white/20 text-white hover:bg-white/30 backdrop-blur-sm border border-white/30'
                  }`}
                >
                  <span className="text-xl mr-1">{cat.icon}</span>
                  <span className="hidden sm:inline text-sm">{cat.label}</span>
                </button>
              ))}
            </div>
          </div>

          {/* FAQ Items */}
          <div className="space-y-4">
            {filteredFAQs.length === 0 ? (
              <div className="text-center py-12 glass-card">
                <p className="text-xl text-gray-600 font-inter">No FAQs found in this category.</p>
              </div>
            ) : (
              filteredFAQs.map(faq => (
                <div
                  key={faq.id}
                  className="glass-card hover:shadow-xl transition-all duration-300 cursor-pointer group"
                  onClick={() => setExpandedId(expandedId === faq.id ? null : faq.id)}
                >
                  <div className="flex items-start justify-between">
                    <h3 className="text-lg font-sora font-bold text-gray-800 flex-1 group-hover:text-blue-600 transition-colors duration-300">
                      {faq.question}
                    </h3>
                    <span className={`text-2xl ml-4 flex-shrink-0 transition-transform duration-300 ${expandedId === faq.id ? 'rotate-180' : ''}`}>
                      ▼
                    </span>
                  </div>
                  
                  {expandedId === faq.id && (
                    <div className="mt-4 pt-4 border-t border-gray-200 animate-in fade-in duration-300">
                      <p className="text-gray-700 font-inter leading-relaxed">
                        {faq.answer}
                      </p>
                    </div>
                  )}
                </div>
              ))
            )}
          </div>

          {/* Still Need Help */}
          <div className="mt-12 glass-card bg-gradient-to-r from-yellow-100/50 to-orange-100/50 border-2 border-yellow-300/50 text-center">
            <h3 className="text-2xl font-sora font-bold text-gray-800 mb-3">Still need help?</h3>
            <p className="text-gray-700 font-inter mb-4">
              If you can't find the answer you're looking for, try refreshing the page or logging out and back in to reset the app.
            </p>
            <div className="flex gap-3 justify-center flex-wrap">
              <Link 
                href="/"
                className="btn-primary inline-block"
              >
                Back to Home
              </Link>
              <Link 
                href="/login"
                className="btn-secondary inline-block"
              >
                Back to Login
              </Link>
            </div>
          </div>
        </div>

        {/* Footer */}
        <div className="text-center py-8 text-white/80 font-inter">
          <p>C.A.S.H. - Learn Money Management Today! 💰</p>
        </div>
      </div>
    </div>
  )
}
