# KidSaver - Learning ATM System

An educational ATM system designed for children to learn about saving money, with powerful tracking tools for parents.

## Getting Started

### Installation

```bash
npm install
```

### Run Development Server

```bash
npm run dev
```

Open [http://localhost:3000](http://localhost:3000) in your browser.

## Features

- 🎯 Goal tracking with visual progress bars
- 💰 Real-time balance updates
- 📊 Statistics and analytics for parents
- 🔥 Firebase backend integration
- 📱 Responsive design for all devices
- 🎨 Beautiful, kid-friendly UI

## Tech Stack

- **Frontend**: Next.js 14, React, TypeScript
- **Styling**: Tailwind CSS
- **Backend**: Firebase (Realtime Database)
- **Charts**: Chart.js
- **Hosting**: Vercel (frontend), Firebase (backend)

## Project Structure

```
atm-learning-system/
├── app/
│   ├── page.tsx          # Welcome page
│   ├── login/            # Login page
│   ├── dashboard/        # Main dashboard
│   └── layout.tsx
├── components/           # Reusable components
├── lib/                  # Firebase config, utilities
└── public/               # Static assets
```

## Deployment

```bash
npm run build
```

Deploy to Vercel with one click!

## License

MIT
