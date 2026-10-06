const express = require('express')
const http = require('http')
const cors = require('cors')
const path = require('path')
require('dotenv').config({ path: path.join(__dirname, '../.env') })

const { syncDatabase, testConnection } = require('./models')
const { initSocket } = require('./services/socketService')
const { startScheduler } = require('./services/scheduler')

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

  server.listen(PORT, () => {
    console.log(`📡 Backend server running on http://localhost:${PORT}`)
  })
}

bootstrap()

module.exports = { app, server }
