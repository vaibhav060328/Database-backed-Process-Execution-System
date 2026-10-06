const { DataTypes } = require('sequelize')
const { sequelize } = require('../db')

const Job = sequelize.define('Job', {
  id: {
    type: DataTypes.INTEGER,
    primaryKey: true,
    autoIncrement: true,
  },
  name: {
    type: DataTypes.STRING,
    allowNull: false,
  },
  status: {
    type: DataTypes.ENUM('queued', 'running', 'done', 'failed', 'cancelled'),
    defaultValue: 'queued',
    allowNull: false,
  },
  storageKey: {
    type: DataTypes.STRING,
    allowNull: true,
  },
  fileUrl: {
    type: DataTypes.STRING,
    allowNull: true,
  },
  fileName: {
    type: DataTypes.STRING,
    allowNull: true,
  },
  fileType: {
    type: DataTypes.STRING,
    allowNull: true,
  },
  args: {
    type: DataTypes.TEXT,
    allowNull: true,
  },
  env: {
    type: DataTypes.TEXT,
    allowNull: true,
    defaultValue: '{}',
  },
  exitCode: {
    type: DataTypes.INTEGER,
    allowNull: true,
  },
  workerId: {
    type: DataTypes.STRING,
    allowNull: true,
  },
  startedAt: {
    type: DataTypes.DATE,
    allowNull: true,
  },
  finishedAt: {
    type: DataTypes.DATE,
    allowNull: true,
  },
}, {
  tableName: 'jobs',
  timestamps: true,
  indexes: [
    { fields: ['status'] },
    { fields: ['workerId'] },
    { fields: ['createdAt'] },
  ],
})

module.exports = Job
