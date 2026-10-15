const express = require('express')
const http = require('http')
const cors = require('cors')
const path = require('path')
require('dotenv').config({ path: path.join(__dirname, '../.env') })

const { syncDatabase, testConnection } = require('./models')
const { initSocket } = require('./services/socketService')
const { startScheduler, stopScheduler } = require('./services/scheduler')
const { startListener, stopListener } = require('./services/listener')

const jobsRouter = require('./routes/jobs')
const dashboardRouter = require('./routes/dashboard')
const workersRouter = require('./routes/workers')

const app = express()
const server = http.createServer(app)

// Initialize Socket.io
initSocket(server)

// Middlewares
app.use(cors({ origin: '*' }))
app.use(express.json())
app.use(express.urlencoded({ extended: true }))

// Health check endpoint
app.get('/api/health', (req, res) => {
  res.json({ status: 'ok', timestamp: new Date().toISOString() })
})

// API Routes
app.use('/api/jobs', jobsRouter)
app.use('/api/dashboard', dashboardRouter)
app.use('/api/workers', workersRouter)

// 404 Handler
app.use((req, res) => {
  res.status(404).json({ message: `Route ${req.method} ${req.originalUrl} not found` })
})

// Global Error Handler
app.use((err, req, res, next) => {
  console.error('Unhandled Server Error:', err)
  res.status(500).json({ message: err.message || 'Internal Server Error' })
})

const PORT = process.env.PORT || 3000

async function bootstrap() {
  console.log('🚀 Starting Process Execution System Backend...')
  
  if (process.env.DATABASE_URL && process.env.DATABASE_URL.trim() !== '') {
    const connected = await testConnection()
    if (connected) {
      await syncDatabase()
    }
  } else {
    console.warn('⚠️ No DATABASE_URL provided in .env. Skipping DB connection.')
  }

  // Start background worker scheduler
  startScheduler()

  // Bridge PostgreSQL NOTIFY events to Socket.io. The C++ dispatcher writes
  // job status and logs straight to the database, so without this the
  // dashboard would never see them.
  startListener()

  server.listen(PORT, () => {
    console.log(`📡 Backend server running on http://localhost:${PORT}`)
  })
}

/**
 * Shut down cleanly.
 *
 * Without this the process exits holding an open PostgreSQL LISTEN connection
 * and a running interval timer. stopListener and stopScheduler already existed
 * but nothing ever called them.
 */
let shuttingDown = false
async function shutdown(signal) {
  if (shuttingDown) return
  shuttingDown = true
  console.log(`\n${signal} received, shutting down...`)

  stopScheduler()
  await stopListener()
  server.close(() => console.log('👋 Backend stopped.'))

  // Don't hang forever on a connection that refuses to close.
  setTimeout(() => process.exit(0), 5000).unref()
}

process.on('SIGINT', () => shutdown('SIGINT'))
process.on('SIGTERM', () => shutdown('SIGTERM'))

bootstrap()

module.exports = { app, server }
