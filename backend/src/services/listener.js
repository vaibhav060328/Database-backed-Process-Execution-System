/**
 * listener.js
 * Bridges PostgreSQL NOTIFY events to Socket.io.
 *
 * Why this exists: the C++ dispatcher writes job status and logs directly to
 * PostgreSQL, so those changes never pass through an Express route and the
 * backend has no other way to learn about them. Database triggers (see
 * db/notify_triggers.sql) announce each change; this holds a LISTEN connection
 * and re-broadcasts over the existing socket events, so the frontend needs no
 * changes at all.
 *
 * It must use a DIRECT (non-pooled) connection. A pooler in transaction mode
 * accepts LISTEN and then silently drops every notification - no error, no
 * delivery. That is why DATABASE_URL_DIRECT exists separately from DATABASE_URL.
 */
const { Client } = require('pg')
const { emitJobStatus, emitJobLog, emitWorkerUpdate } = require('./socketService')

const CHANNELS = ['job_events', 'job_log_events', 'worker_events']

let client = null
let stopped = false
let retryDelayMs = 1000
const MAX_RETRY_MS = 30000

function directUrl() {
  if (process.env.DATABASE_URL_DIRECT) return process.env.DATABASE_URL_DIRECT

  // Fall back to deriving it: Neon's pooled host is the direct host plus
  // "-pooler". Listening on the pooled endpoint would silently receive nothing.
  const pooled = process.env.DATABASE_URL
  if (!pooled) return null
  try {
    const u = new URL(pooled)
    if (!u.hostname.includes('-pooler')) return pooled
    u.hostname = u.hostname.replace('-pooler', '')
    console.warn('[Listener] DATABASE_URL_DIRECT not set; derived a direct URL by removing "-pooler".')
    return u.toString()
  } catch {
    return null
  }
}

function handle(channel, payloadText) {
  let p
  try {
    p = JSON.parse(payloadText)
  } catch {
    console.warn(`[Listener] ignoring malformed payload on ${channel}`)
    return
  }

  if (channel === 'job_events') {
    emitJobStatus(p.id, p.status, {
      workerId: p.workerId ?? null,
      exitCode: p.exitCode ?? null,
      name: p.name,
      fileName: p.fileName,
    })
  } else if (channel === 'job_log_events') {
    emitJobLog(p.id, p.line, p.level)
  } else if (channel === 'worker_events') {
    emitWorkerUpdate({
      id: p.id,
      status: p.status,
      cpu: p.cpu ?? 0,
      memory: p.memory ?? 0,
      currentJobId: p.currentJobId ?? null,
      lastHeartbeat: p.lastHeartbeat ?? new Date().toISOString(),
    })
  }
}

async function connect() {
  if (stopped) return

  const url = directUrl()
  if (!url) {
    console.warn('[Listener] No database URL available; live updates are disabled.')
    return
  }

  client = new Client({ connectionString: url })

  client.on('notification', (msg) => handle(msg.channel, msg.payload))

  // A dropped connection must not take the process down, and must not leave
  // the dashboard silently stale: schedule a reconnect and re-LISTEN.
  client.on('error', (err) => {
    console.error('[Listener] connection error:', err.message)
    scheduleReconnect()
  })
  client.on('end', () => {
    if (!stopped) scheduleReconnect()
  })

  try {
    await client.connect()
    for (const ch of CHANNELS) await client.query(`LISTEN ${ch}`)
    retryDelayMs = 1000
    console.log(`📡 Listening for database events on: ${CHANNELS.join(', ')}`)
  } catch (err) {
    console.error('[Listener] failed to connect:', err.message)
    scheduleReconnect()
  }
}

let retryTimer = null
function scheduleReconnect() {
  if (stopped || retryTimer) return

  // Neon suspends idle connections, so a drop is expected rather than
  // exceptional. Back off so a database that is down is not hammered.
  const delay = retryDelayMs
  retryDelayMs = Math.min(retryDelayMs * 2, MAX_RETRY_MS)

  retryTimer = setTimeout(async () => {
    retryTimer = null
    if (client) {
      try { await client.end() } catch { /* already gone */ }
      client = null
    }
    console.log(`[Listener] reconnecting (next backoff ${retryDelayMs}ms)...`)
    connect()
  }, delay)

  if (typeof retryTimer.unref === 'function') retryTimer.unref()
}

function startListener() {
  stopped = false
  connect()
}

async function stopListener() {
  stopped = true
  if (retryTimer) { clearTimeout(retryTimer); retryTimer = null }
  if (client) {
    try { await client.end() } catch { /* already gone */ }
    client = null
  }
}

module.exports = { startListener, stopListener }
