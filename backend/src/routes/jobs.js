const express = require('express')
const router = express.Router()
const { Op } = require('sequelize')
const path = require('path')
const { Job, JobLog, Worker } = require('../models')
const { upload } = require('../middlewares/upload')
const { uploadFile, getFileStream } = require('../services/storageService')

/**
 * GET /api/jobs
 * List jobs with pagination, status filter, and search
 */
router.get('/', async (req, res) => {
  try {
    const page = Math.max(1, parseInt(req.query.page || '1', 10))
    const limit = Math.max(1, Math.min(100, parseInt(req.query.limit || '10', 10)))
    const offset = (page - 1) * limit
    const { status, search } = req.query

    const where = {}
    if (status && status !== 'all') {
      where.status = status
    }
    if (search && search.trim() !== '') {
      where[Op.or] = [
        { name: { [Op.iLike ? Op.iLike : Op.like]: `%${search.trim()}%` } },
        { fileName: { [Op.iLike ? Op.iLike : Op.like]: `%${search.trim()}%` } },
      ]
    }

    const { rows: jobs, count: total } = await Job.findAndCountAll({
      where,
      order: [['createdAt', 'DESC']],
      limit,
      offset,
    })

    res.json({
      jobs,
      total,
      page,
      limit,
    })
  } catch (error) {
    console.error('Error fetching jobs:', error)
    res.status(500).json({ message: error.message })
  }
})

/**
 * GET /api/jobs/:id
 * Retrieve a specific job with logs
 */
router.get('/:id', async (req, res) => {
  try {
    /*
     * Order the logs at the TOP level, not inside the include.
     *
     * Sequelize silently ignores `order` within an include for a hasMany
     * association, so logs came back in whatever order PostgreSQL happened to
     * return them - the "Job created" line written at submission time showed up
     * last in the viewer. Sorting by id as well as timestamp breaks ties, since
     * several lines can share the same second.
     */
    const job = await Job.findByPk(req.params.id, {
      include: [
        { model: JobLog, as: 'logs' },
        { model: Worker, as: 'worker' },
      ],
      order: [
        [{ model: JobLog, as: 'logs' }, 'timestamp', 'ASC'],
        [{ model: JobLog, as: 'logs' }, 'id', 'ASC'],
      ],
    })

    if (!job) {
      return res.status(404).json({ message: `Job #${req.params.id} not found` })
    }

    res.json(job)
  } catch (error) {
    console.error('Error fetching job details:', error)
    res.status(500).json({ message: error.message })
  }
})

/**
 * GET /api/jobs/:id/file
 * Download / stream the executable or script from Neon Object Storage
 */
router.get('/:id/file', async (req, res) => {
  try {
    const job = await Job.findByPk(req.params.id)
    if (!job) {
      return res.status(404).json({ message: 'Job not found' })
    }

    if (!job.storageKey) {
      return res.status(404).json({ message: 'No file stored for this job' })
    }

    const fileStream = await getFileStream(job.storageKey)
    
    res.setHeader('Content-Disposition', `attachment; filename="${job.fileName || 'script'}"`)
    res.setHeader('Content-Type', 'application/octet-stream')

    if (typeof fileStream.pipe === 'function') {
      fileStream.pipe(res)
    } else {
      for await (const chunk of fileStream) {
        res.write(chunk)
      }
      res.end()
    }
  } catch (error) {
    console.error('Error streaming job file:', error)
    res.status(500).json({ message: error.message })
  }
})

/**
 * POST /api/jobs
 * Submit a new job with file upload directly to Neon Object Storage
 */
router.post('/', upload.single('file'), async (req, res) => {
  try {
    const { name, args, env } = req.body
    const file = req.file

    if (!file) {
      return res.status(400).json({ message: 'Executable or script file is required' })
    }

    const ext = path.extname(file.originalname).toLowerCase()
    const jobName = name?.trim() || path.basename(file.originalname, ext)

    // Upload directly to Neon Object Storage
    const storageResult = await uploadFile(file.buffer, file.originalname, file.mimetype)

    const newJob = await Job.create({
      name: jobName,
      status: 'queued',
      storageKey: storageResult.key,
      fileUrl: storageResult.location,
      fileName: file.originalname,
      fileType: ext || 'script',
      args: args ? args.trim() : null,
      env: env || '{}',
    })

    // Initial log entry
    await JobLog.create({
      jobId: newJob.id,
      line: `Job created with file '${file.originalname}' stored in Neon Object Storage and queued.`,
      level: 'info',
    })

    /*
     * No socket emit here.
     *
     * Every write to jobs/job_logs fires a database trigger that NOTIFYs, and
     * services/listener.js re-broadcasts it. Emitting from the route as well
     * delivered each change twice - visible as duplicated rows in the
     * dashboard's activity feed. The database is now the single source of
     * events, which also covers the writes the C++ dispatcher makes directly
     * and which never pass through Express at all.
     */

    res.status(201).json({
      id: newJob.id,
      message: 'Job submitted successfully',
      job: newJob,
    })
  } catch (error) {
    console.error('Error submitting job:', error)
    res.status(500).json({ message: error.message })
  }
})

/**
 * DELETE /api/jobs/:id
 * Cancel an active job
 */
router.delete('/:id', async (req, res) => {
  try {
    const job = await Job.findByPk(req.params.id)
    if (!job) {
      return res.status(404).json({ message: `Job #${req.params.id} not found` })
    }

    if (['done', 'failed', 'cancelled'].includes(job.status)) {
      return res.status(400).json({ message: `Job is already ${job.status}` })
    }

    job.status = 'cancelled'
    job.finishedAt = new Date()
    await job.save()

    await JobLog.create({
      jobId: job.id,
      line: 'Job cancelled by user request.',
      level: 'warn',
    })

    // Events come from the database trigger; see the note on job creation.

    res.json({ message: 'Job cancelled successfully', job })
  } catch (error) {
    console.error('Error cancelling job:', error)
    res.status(500).json({ message: error.message })
  }
})

/**
 * POST /api/jobs/:id/logs
 * Helper endpoint for workers to post execution logs
 */
router.post('/:id/logs', async (req, res) => {
  try {
    const { line, level } = req.body
    const jobId = parseInt(req.params.id, 10)

    if (!line) {
      return res.status(400).json({ message: 'Log line is required' })
    }

    const logEntry = await JobLog.create({
      jobId,
      line,
      level: level || 'info',
    })

    res.status(201).json({ success: true, log: logEntry })
  } catch (error) {
    res.status(500).json({ message: error.message })
  }
})

/**
 * PATCH /api/jobs/:id/status
 * Helper endpoint for workers to update job status
 */
router.patch('/:id/status', async (req, res) => {
  try {
    const jobId = parseInt(req.params.id, 10)
    const { status, exitCode, workerId, startedAt, finishedAt } = req.body

    const job = await Job.findByPk(jobId)
    if (!job) {
      return res.status(404).json({ message: 'Job not found' })
    }

    if (status) job.status = status
    if (exitCode !== undefined) job.exitCode = exitCode
    if (workerId !== undefined) job.workerId = workerId
    if (startedAt) job.startedAt = startedAt
    if (finishedAt) job.finishedAt = finishedAt

    await job.save()

    res.json({ success: true, job })
  } catch (error) {
    res.status(500).json({ message: error.message })
  }
})

module.exports = router
