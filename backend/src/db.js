const { Sequelize } = require('sequelize')
const path = require('path')
require('dotenv').config({ path: path.join(__dirname, '../.env') })

let sequelize

if (process.env.DATABASE_URL && process.env.DATABASE_URL.trim() !== '') {
  const isNeonOrCloud = process.env.DATABASE_URL.includes('neon.tech') || 
                        process.env.DATABASE_URL.includes('sslmode=require') ||
                        process.env.DB_SSL === 'true'

  sequelize = new Sequelize(process.env.DATABASE_URL, {
    dialect: 'postgres',
    logging: process.env.NODE_ENV === 'development' ? console.log : false,
    dialectOptions: isNeonOrCloud ? {
      ssl: {
        require: true,
        rejectUnauthorized: false,
      },
    } : {},
    pool: {
      max: 10,
      min: 0,
      acquire: 30000,
      idle: 10000,
    },
  })
} else {
  const isSsl = process.env.DB_SSL === 'true'
  sequelize = new Sequelize(
    process.env.DB_NAME || 'neondb',
    process.env.DB_USER || 'postgres',
    process.env.DB_PASSWORD || '',
    {
      host: process.env.DB_HOST || 'localhost',
      port: parseInt(process.env.DB_PORT || '5432', 10),
      dialect: 'postgres',
      logging: process.env.NODE_ENV === 'development' ? console.log : false,
      dialectOptions: isSsl ? {
        ssl: {
          require: true,
          rejectUnauthorized: false,
        },
      } : {},
      pool: {
        max: 10,
        min: 0,
        acquire: 30000,
        idle: 10000,
      },
    }
  )
}

async function testConnection() {
  try {
    await sequelize.authenticate()
    console.log('✅ Connected to PostgreSQL database successfully.')
    return true
  } catch (error) {
    console.error('❌ Unable to connect to the database:', error.message)
    return false
  }
}

module.exports = {
  sequelize,
  testConnection,
}
