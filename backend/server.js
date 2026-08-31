import express from 'express'
import path from 'node:path'
import fs from 'node:fs'
import { fileURLToPath } from 'node:url'

const app = express()
const root = path.dirname(fileURLToPath(import.meta.url))
const frontendDist = path.join(root, '..', 'frontend', 'dist')
const port = Number(process.env.PORT || 5000)

app.use((req, res, next) => {
  res.setHeader('Access-Control-Allow-Origin', process.env.FRONTEND_ORIGIN || '*')
  res.setHeader('Access-Control-Allow-Methods', 'GET, OPTIONS')
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type')
  if (req.method === 'OPTIONS') return res.sendStatus(204)
  next()
})

app.get('/api/health', (_req, res) => res.json({ ok: true, service: 'robot-arm-vision-api' }))

function httpUrl(value) {
  try { const url = new URL(value); return ['http:', 'https:'].includes(url.protocol) ? url : null } catch { return null }
}

function snapshotUrl(value) {
  const url = httpUrl(value)
  if (!url) return null
  if (/\/video\/?$/i.test(url.pathname)) url.pathname = url.pathname.replace(/\/video\/?$/i, '/shot.jpg')
  if (url.pathname === '/' || !url.pathname) url.pathname = '/shot.jpg'
  return url
}

app.get('/api/mobile-frame', async (req, res) => {
  const target = snapshotUrl(req.query.url)
  if (!target) return res.status(400).json({ error: 'Invalid mobile camera URL' })
  try {
    const response = await fetch(target, { signal: AbortSignal.timeout(2500), cache: 'no-store' })
    if (!response.ok) throw new Error(`Camera returned ${response.status}`)
    res.set({ 'Content-Type': response.headers.get('content-type') || 'image/jpeg', 'Cache-Control': 'no-store' })
    res.send(Buffer.from(await response.arrayBuffer()))
  } catch (error) { res.status(502).json({ error: error.message }) }
})

app.get('/api/control', async (req, res) => {
  const base = httpUrl(req.query.controller)
  const pattern = String(req.query.pattern || ''), dir = String(req.query.dir || '')
  if (!base || !/^[01],[01],[01],[01],[01]$/.test(pattern) || !/^(left|right|center)$/.test(dir)) return res.status(400).json({ error: 'Invalid request' })
  const target = new URL('/control', base); target.searchParams.set('pattern', pattern); target.searchParams.set('dir', dir)
  try {
    const response = await fetch(target, { signal: AbortSignal.timeout(7000) })
    res.status(response.status).type(response.headers.get('content-type') || 'text/plain').send(await response.text())
  } catch (error) { res.status(502).json({ error: error.message }) }
})

app.get('/api/device-status', async (req, res) => {
  const target = httpUrl(req.query.controller)
  if (!target) return res.status(400).json({ online: false, error: 'Invalid controller URL' })
  try {
    const response = await fetch(target, { signal: AbortSignal.timeout(1500), cache: 'no-store' })
    const body = await response.text()
    const online = response.ok && body.toLowerCase().includes('robot arm controller online')
    res.set('Cache-Control', 'no-store').json({ online, status: response.status, ip: target.hostname })
  } catch (error) {
    res.set('Cache-Control', 'no-store').status(200).json({ online: false, error: error.message, ip: target.hostname })
  }
})

app.get('/api/servo', async (req, res) => {
  const controller = httpUrl(req.query.controller)
  const angles = ['base', 'shoulder', 'elbow'].map(name => Number(req.query[name]))
  if (!controller || angles.some(value => !Number.isInteger(value) || value < 0 || value > 180)) {
    return res.status(400).json({ ok: false, error: 'Controller and angles 0-180 are required' })
  }
  const target = new URL('/servo', controller)
  ;['base', 'shoulder', 'elbow'].forEach((name, index) => target.searchParams.set(name, String(angles[index])))
  try {
    const response = await fetch(target, { signal: AbortSignal.timeout(8000) })
    res.status(response.status).type(response.headers.get('content-type') || 'application/json').send(await response.text())
  } catch (error) { res.status(502).json({ ok: false, error: error.message }) }
})

if (fs.existsSync(path.join(frontendDist, 'index.html'))) {
  app.use(express.static(frontendDist))
  app.get(/.*/, (_req, res) => res.sendFile(path.join(frontendDist, 'index.html')))
} else {
  app.get('/', (_req, res) => res.json({ ok: true, service: 'robot-arm-vision-api', health: '/api/health' }))
}
const httpServer = app.listen(port, '0.0.0.0', () => {
  console.log(`Backend running: http://localhost:${port}`)
})

httpServer.on('error', error => {
  if (error.code === 'EADDRINUSE') {
    console.error(`Port ${port} is already in use. Stop the old server or set another PORT.`)
  } else {
    console.error(error)
  }
  process.exitCode = 1
})

// Explicitly keep the HTTP listener alive, including under newer Node watch mode.
httpServer.ref()
