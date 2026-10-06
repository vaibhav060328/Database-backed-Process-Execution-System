const express = require('express')
const router = express.Router()
const { Op } = require('sequelize')
const { Job, Worker } = require('../models')

/**
 * GET /api/dashboard
 * Aggregated dashboard statistics and time-series chart data
 */
router.get('/', async (req, res) => {
  try {
    const range = req.query.range || 'minutely'

    // Summary counts
    const [total, running, failed, workers] = await Promise.all([
      Job.count(),
      Job.count({ where: { status: 'running' } }),
      Job.count({ where: { status: 'failed' } }),
      Worker.count({ where: { status: { [Op.in]: ['online', 'busy'] } } }),
    ])

    // Generate intervals for chart
    const now = new Date()
    const intervals = []
    let stepMs = 60 * 1000 // default minutely (1 min)
    let count = 10

    if (range === 'secondly') {
      stepMs = 5 * 1000 // 5 seconds interval, 12 points
      count = 12
    } else if (range === 'minutely') {
      stepMs = 60 * 1000 // 1 minute interval, 10 points
      count = 10
    } else if (range === 'hourly') {
      stepMs = 60 * 60 * 1000 // 1 hour interval, 12 points
      count = 12
    } else if (range === 'daily') {
      stepMs = 24 * 60 * 60 * 1000 // 1 day interval, 7 points
      count = 7
    }

    for (let i = count - 1; i >= 0; i--) {
      const start = new Date(now.getTime() - (i + 1) * stepMs)
      const end = new Date(now.getTime() - i * stepMs)
      
      let timeLabel
      if (range === 'secondly') {
        timeLabel = end.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })
      } else if (range === 'minutely') {
        timeLabel = end.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
      } else if (range === 'hourly') {
        timeLabel = `${end.getHours()}:00`
      } else {
        timeLabel = end.toLocaleDateString([], { month: 'short', day: 'numeric' })
      }

      intervals.push({ start, end, time: timeLabel })
    }

    // Fetch all jobs updated or created in the window
    const windowStart = intervals[0].start
    const recentJobs = await Job.findAll({
      where: {
        createdAt: {
          [Op.gte]: windowStart,
        },
      },
      attributes: ['id', 'status', 'createdAt'],
    })

    const chart = intervals.map(({ start, end, time }) => {
      const bucketJobs = recentJobs.filter((j) => {
        const t = new Date(j.createdAt)
        return t >= start && t < end
      })

      const queued = bucketJobs.filter((j) => j.status === 'queued').length
      const running = bucketJobs.filter((j) => j.status === 'running').length
      const done = bucketJobs.filter((j) => j.status === 'done').length
      const failed = bucketJobs.filter((j) => j.status === 'failed').length

      return {
        time,
        queued,
        running,
        done,
        failed,
        total: bucketJobs.length,
      }
    })

    res.json({
      total,
      running,
      failed,
      workers,
      chart,
    })
  } catch (error) {
    console.error('Error fetching dashboard stats:', error)
    res.status(500).json({ message: error.message })
  }
})

module.exports = router
