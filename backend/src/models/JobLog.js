const { DataTypes } = require('sequelize')
const { sequelize } = require('../db')

const JobLog = sequelize.define('JobLog', {
  id: {
    type: DataTypes.INTEGER,
    primaryKey: true,
    autoIncrement: true,
  },
  jobId: {
    type: DataTypes.INTEGER,
    allowNull: false,
  },
  line: {
    type: DataTypes.TEXT,
    allowNull: false,
  },
  level: {
    type: DataTypes.ENUM('info', 'warn', 'error', 'success'),
    defaultValue: 'info',
    allowNull: false,
  },
  timestamp: {
    type: DataTypes.DATE,
    defaultValue: DataTypes.NOW,
  },
}, {
  tableName: 'job_logs',
  timestamps: false,
  indexes: [
    { fields: ['jobId'] },
    { fields: ['timestamp'] },
  ],
})

module.exports = JobLog
