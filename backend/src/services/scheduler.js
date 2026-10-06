/**
 * scheduler.js
 * Optional periodic maintenance service (stale worker cleanup, timeout checks).
 */

const { Op } = require('sequelize')
const { Worker, Job } = require('../models')
const { emitWorkerUpdate, emitJobStatus, emitJobLog } = require('./socketService')

let timer = null

function startScheduler(intervalMs = 10000) {
  if (timer) return

  timer = setInterval(async () => {
    try {
      // 1. Mark stale workers as offline (no heartbeat in 30s)
      const staleThreshold = new Date(Date.now() - 30 * 1000)
      const staleWorkers = await Worker.findAll({
        where: {
          lastHeartbeat: { [Op.lt]: staleThreshold },
          status: { [Op.ne]: 'offline' },
        },
      })

      for (const worker of staleWorkers) {
        worker.status = 'offline'
        await worker.save()
        emitWorkerUpdate(worker)
        console.log(`[Scheduler] Worker ${worker.id} marked offline due to inactivity.`)
      }
    } catch (err) {
      console.error('[Scheduler Error]', err.message)
    }
  }, intervalMs)

  console.log('⏰ Backend maintenance scheduler started.')
}

function stopScheduler() {
  if (timer) {
    clearInterval(timer)
    timer = null
  }
}

module.exports = {
  startScheduler,
  stopScheduler,
}
