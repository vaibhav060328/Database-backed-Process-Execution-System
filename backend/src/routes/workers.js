const express = require('express')
const router = express.Router()
const { Op } = require('sequelize')
const { Worker, Job } = require('../models')
const { emitWorkerUpdate } = require('../services/socketService')

/**
 * GET /api/workers
 * List all workers, automatically marking stale ones as offline
 */
router.get('/', async (req, res) => {
  try {
    const staleThreshold = new Date(Date.now() - 30 * 1000) // 30s threshold

    // Auto mark offline if heartbeat is stale
    await Worker.update(
      { status: 'offline' },
      {
        where: {
          lastHeartbeat: { [Op.lt]: staleThreshold },
          status: { [Op.ne]: 'offline' },
        },
      }
    )

    const workers = await Worker.findAll({
      order: [['id', 'ASC']],
    })

    res.json(workers)
  } catch (error) {
    console.error('Error fetching workers:', error)
    res.status(500).json({ message: error.message })
  }
})

/**
 * POST /api/workers/heartbeat
 * Worker registers or reports heartbeat + metrics
 */
router.post('/heartbeat', async (req, res) => {
  try {
    const { id, name, host, port, status, cpu, memory, currentJobId } = req.body

    if (!id) {
      return res.status(400).json({ message: 'Worker ID is required' })
    }

    const [worker, created] = await Worker.findOrCreate({
      where: { id },
      defaults: {
        id,
        name: name || id,
        host: host || 'localhost',
        port: port || null,
        status: status || 'online',
        cpu: cpu ?? 0,
        memory: memory ?? 0,
        currentJobId: currentJobId ?? null,
        lastHeartbeat: new Date(),
      },
    })

    if (!created) {
      if (name) worker.name = name
      if (host) worker.host = host
      if (port !== undefined) worker.port = port
      if (status) worker.status = status
      if (cpu !== undefined) worker.cpu = cpu
      if (memory !== undefined) worker.memory = memory
      worker.currentJobId = currentJobId ?? null
      worker.lastHeartbeat = new Date()
      await worker.save()
    }

    emitWorkerUpdate(worker)

    res.json({
      success: true,
      worker,
    })
  } catch (error) {
    console.error('Error recording worker heartbeat:', error)
    res.status(500).json({ message: error.message })
  }
})

module.exports = router
