const { sequelize, testConnection } = require('../db')
const Job = require('./Job')
const Worker = require('./Worker')
const JobLog = require('./JobLog')

// Associations
Job.hasMany(JobLog, { foreignKey: 'jobId', as: 'logs', onDelete: 'CASCADE' })
JobLog.belongsTo(Job, { foreignKey: 'jobId' })

Job.belongsTo(Worker, { foreignKey: 'workerId', as: 'worker' })
Worker.hasMany(Job, { foreignKey: 'workerId', as: 'jobs' })

/**
 * Create any missing tables.
 *
 * Deliberately NOT { alter: true } any more. The live-update triggers in
 * db/notify_triggers.sql depend on jobs.status, jobs."workerId" and
 * job_logs.level, and PostgreSQL refuses to alter the type of a column a
 * trigger depends on - so alter would fail on every boot and silently leave the
 * schema unchanged, which is worse than not attempting it.
 *
 * To change the schema:
 *   node db/apply_triggers.js --drop
 *   <apply the change>
 *   node db/apply_triggers.js
 */
async function syncDatabase(force = false) {
  try {
    await sequelize.sync({ force })
    console.log('✅ Database models synchronized successfully with Neon PostgreSQL.')
  } catch (error) {
    console.error('❌ Failed to synchronize database models:', error.message)
  }
}

module.exports = {
  sequelize,
  testConnection,
  Job,
  Worker,
  JobLog,
  syncDatabase,
}
