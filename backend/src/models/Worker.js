const { DataTypes } = require('sequelize')
const { sequelize } = require('../db')

const Worker = sequelize.define('Worker', {
  id: {
    type: DataTypes.STRING,
    primaryKey: true,
    allowNull: false,
  },
  name: {
    type: DataTypes.STRING,
    allowNull: true,
  },
  host: {
    type: DataTypes.STRING,
    defaultValue: 'localhost',
  },
  port: {
    type: DataTypes.INTEGER,
    allowNull: true,
  },
  status: {
    type: DataTypes.ENUM('online', 'busy', 'offline'),
    defaultValue: 'online',
    allowNull: false,
  },
  cpu: {
    type: DataTypes.FLOAT,
    defaultValue: 0,
  },
  memory: {
    type: DataTypes.FLOAT,
    defaultValue: 0,
  },
  currentJobId: {
    type: DataTypes.INTEGER,
    allowNull: true,
  },
  lastHeartbeat: {
    type: DataTypes.DATE,
    defaultValue: DataTypes.NOW,
  },
}, {
  tableName: 'workers',
  timestamps: true,
  indexes: [
    { fields: ['status'] },
    { fields: ['lastHeartbeat'] },
  ],
})

module.exports = Worker
