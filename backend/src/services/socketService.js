let io = null

function initSocket(server) {
  const { Server } = require('socket.io')
  io = new Server(server, {
    cors: {
      origin: '*',
      methods: ['GET', 'POST', 'PUT', 'DELETE'],
    },
  })

  io.on('connection', (socket) => {
    console.log(`🔌 Client connected: ${socket.id}`)

    socket.on('disconnect', () => {
      console.log(`🔌 Client disconnected: ${socket.id}`)
    })
  })

  return io
}

function getIO() {
  return io
}

function emitJobStatus(id, status, extra = {}) {
  if (!io) return
  io.emit('job:status', {
    id,
    status,
    ...extra,
  })
}

function emitJobLog(id, line, level = 'info') {
  if (!io) return
  io.emit('job:log', {
    id,
    line,
    level,
    timestamp: new Date().toISOString(),
  })
}

function emitWorkerUpdate(worker) {
  if (!io) return
  io.emit('worker:update', {
    id: worker.id,
    status: worker.status,
    cpu: worker.cpu ?? 0,
    memory: worker.memory ?? 0,
    currentJobId: worker.currentJobId ?? null,
    lastHeartbeat: worker.lastHeartbeat ?? new Date().toISOString(),
  })
}

module.exports = {
  initSocket,
  getIO,
  emitJobStatus,
  emitJobLog,
  emitWorkerUpdate,
}
