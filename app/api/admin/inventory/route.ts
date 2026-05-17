import { NextResponse } from 'next/server'
import { prisma } from '@/lib/prisma'

function peso(value: number): string {
  return `PHP ${value.toFixed(2)}`
}

export async function GET(request: Request) {
  const inventory = await prisma.machineInventory.findUnique({
    where: { id: 1 },
  })

  const recentDeposits = await prisma.machineCashEvent.findMany({
    where: { direction: 'IN' },
    orderBy: { createdAt: 'desc' },
    take: 10,
  })

  const recentWithdrawals = await prisma.machineCashEvent.findMany({
    where: { direction: 'OUT' },
    orderBy: { createdAt: 'desc' },
    take: 10,
  })

  const payload = {
    currentInventory: inventory,
    recentDeposits: recentDeposits.map(d => ({
      amount: d.amount,
      source: d.source,
      createdAt: d.createdAt,
    })),
    recentWithdrawals: recentWithdrawals.map(w => ({
      amount: w.amount,
      breakdown: w.breakdown,
      createdAt: w.createdAt,
    })),
  }

  const url = new URL(request.url)
  const forceJson = url.searchParams.get('format') === 'json'
  const wantsHtml = (request.headers.get('accept') ?? '').includes('text/html')

  if (!forceJson && wantsHtml) {
    const inv = inventory ?? {
      bill20: 0,
      bill50: 0,
      bill100: 0,
      bill500: 0,
      bill1000: 0,
      coin1: 0,
      coin5: 0,
      coin10: 0,
      coin20: 0,
      updatedAt: null,
    }

    const billTotal = inv.bill20 * 20 + inv.bill50 * 50 + inv.bill100 * 100 + inv.bill500 * 500 + inv.bill1000 * 1000
    const coinTotal = inv.coin1 * 1 + inv.coin5 * 5 + inv.coin10 * 10 + inv.coin20 * 20
    const grandTotal = billTotal + coinTotal

    const depositsRows = payload.recentDeposits
      .map(
        d => `<tr><td>${new Date(d.createdAt).toLocaleString()}</td><td>${d.source ?? 'unknown'}</td><td>${peso(d.amount)}</td></tr>`
      )
      .join('')

    const withdrawalsRows = payload.recentWithdrawals
      .map(w => {
        const breakdown = typeof w.breakdown === 'object' && w.breakdown !== null ? JSON.stringify(w.breakdown) : '-'
        return `<tr><td>${new Date(w.createdAt).toLocaleString()}</td><td>${peso(w.amount)}</td><td><code>${breakdown}</code></td></tr>`
      })
      .join('')

    const html = `<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>ATM Inventory</title>
    <style>
      body { font-family: Segoe UI, Arial, sans-serif; margin: 24px; background: #f7f9fc; color: #1f2937; }
      .card { background: #fff; border: 1px solid #e5e7eb; border-radius: 10px; padding: 16px; margin-bottom: 16px; }
      h1, h2 { margin: 0 0 12px; }
      .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 10px; }
      .stat { border: 1px solid #e5e7eb; border-radius: 8px; padding: 10px; background: #fafafa; }
      .label { font-size: 12px; color: #6b7280; }
      .value { font-size: 20px; font-weight: 700; }
      table { width: 100%; border-collapse: collapse; }
      th, td { text-align: left; border-bottom: 1px solid #e5e7eb; padding: 8px; vertical-align: top; }
      th { background: #f3f4f6; }
      code { white-space: pre-wrap; word-break: break-word; }
    </style>
  </head>
  <body>
    <h1>Machine Inventory Dashboard</h1>
    <div class="card">
      <h2>Current Totals</h2>
      <div class="grid">
        <div class="stat"><div class="label">Bills Total</div><div class="value">${peso(billTotal)}</div></div>
        <div class="stat"><div class="label">Coins Total</div><div class="value">${peso(coinTotal)}</div></div>
        <div class="stat"><div class="label">Grand Total</div><div class="value">${peso(grandTotal)}</div></div>
        <div class="stat"><div class="label">Last Updated</div><div class="value" style="font-size:14px">${inv.updatedAt ? new Date(inv.updatedAt).toLocaleString() : '-'}</div></div>
      </div>
    </div>

    <div class="card">
      <h2>Denomination Counts</h2>
      <div class="grid">
        <div class="stat"><div class="label">bill20</div><div class="value">${inv.bill20}</div></div>
        <div class="stat"><div class="label">bill50</div><div class="value">${inv.bill50}</div></div>
        <div class="stat"><div class="label">bill100</div><div class="value">${inv.bill100}</div></div>
        <div class="stat"><div class="label">bill500</div><div class="value">${inv.bill500}</div></div>
        <div class="stat"><div class="label">bill1000</div><div class="value">${inv.bill1000}</div></div>
        <div class="stat"><div class="label">coin1</div><div class="value">${inv.coin1}</div></div>
        <div class="stat"><div class="label">coin5</div><div class="value">${inv.coin5}</div></div>
        <div class="stat"><div class="label">coin10</div><div class="value">${inv.coin10}</div></div>
        <div class="stat"><div class="label">coin20</div><div class="value">${inv.coin20}</div></div>
      </div>
    </div>

    <div class="card">
      <h2>Recent Deposits (10)</h2>
      <table>
        <thead><tr><th>Time</th><th>Source</th><th>Amount</th></tr></thead>
        <tbody>${depositsRows || '<tr><td colspan="3">No deposits yet</td></tr>'}</tbody>
      </table>
    </div>

    <div class="card">
      <h2>Recent Withdrawals (10)</h2>
      <table>
        <thead><tr><th>Time</th><th>Amount</th><th>Breakdown</th></tr></thead>
        <tbody>${withdrawalsRows || '<tr><td colspan="3">No withdrawals yet</td></tr>'}</tbody>
      </table>
    </div>
  </body>
</html>`

    return new Response(html, {
      headers: { 'Content-Type': 'text/html; charset=utf-8' },
    })
  }

  return NextResponse.json(payload)
}

const INVENTORY_FIELDS = [
  'bill20',
  'bill50',
  'bill100',
  'bill500',
  'bill1000',
  'coin1',
  'coin5',
  'coin10',
  'coin20',
] as const

type InventoryField = (typeof INVENTORY_FIELDS)[number]

function pickInventoryUpdates(input: Record<string, unknown>): Partial<Record<InventoryField, number>> {
  const out: Partial<Record<InventoryField, number>> = {}
  for (const field of INVENTORY_FIELDS) {
    const raw = input[field]
    if (raw === undefined || raw === null) continue
    const n = typeof raw === 'number' ? raw : Number(raw)
    if (!Number.isFinite(n) || n < 0 || !Number.isInteger(n)) {
      throw new Error(`Invalid value for ${field}: ${raw}`)
    }
    out[field] = n
  }
  return out
}

export async function POST(request: Request) {
  const body = await request.json()

  if (body.action === 'reset') {
    await prisma.machineInventory.update({
      where: { id: 1 },
      data: {
        bill20: 0,
        bill50: 0,
        bill100: 0,
        bill500: 0,
        bill1000: 0,
        coin1: 0,
        coin5: 0,
        coin10: 0,
        coin20: 0,
      },
    })

    return NextResponse.json({ status: 'reset complete' })
  }

  // Set absolute counts for any subset of denominations.
  // Body: { "action": "set", "bill20": 5, "bill50": 5, ... }
  if (body.action === 'set') {
    let updates: Partial<Record<InventoryField, number>>
    try {
      updates = pickInventoryUpdates(body as Record<string, unknown>)
    } catch (err) {
      return NextResponse.json({ error: (err as Error).message }, { status: 400 })
    }
    if (Object.keys(updates).length === 0) {
      return NextResponse.json({ error: 'No inventory fields provided' }, { status: 400 })
    }
    const inventory = await prisma.machineInventory.upsert({
      where: { id: 1 },
      create: { id: 1, ...updates },
      update: updates,
    })
    return NextResponse.json({ status: 'set complete', inventory })
  }

  return NextResponse.json({ error: 'Unknown action' }, { status: 400 })
}
