import type { Metadata } from 'next'
import './globals.css'

export const metadata: Metadata = {
  title: 'C.A.S.H. - Learning ATM System',
  description: 'Educational ATM system for kids to learn about saving money',
}

export default function RootLayout({
  children,
}: {
  children: React.ReactNode
}) {
  return (
    <html lang="en">
      <head>
        <meta charSet="utf-8" />
      </head>
      <body>{children}</body>
    </html>
  )
}
