const { sequelize, testConnection } = require('../db')
const Job = require('./Job')
const Worker = require('./Worker')
const JobLog = require('./JobLog')

// Associations
Job.hasMany(JobLog, { foreignKey: 'jobId', as: 'logs', onDelete: 'CASCADE' })
JobLog.belongsTo(Job, { foreignKey: 'jobId' })

Job.belongsTo(Worker, { foreignKey: 'workerId', as: 'worker' })
Worker.hasMany(Job, { foreignKey: 'workerId', as: 'jobs' })

async function syncDatabase(force = false) {
  try {
    await sequelize.sync({ alter: !force, force })
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
