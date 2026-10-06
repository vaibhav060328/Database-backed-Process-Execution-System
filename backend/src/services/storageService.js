const {
  S3Client,
  PutObjectCommand,
  GetObjectCommand,
  DeleteObjectCommand,
} = require('@aws-sdk/client-s3')
const path = require('path')

const endpoint = process.env.NEON_STORAGE_ENDPOINT || process.env.AWS_ENDPOINT_URL_S3
const accessKeyId = process.env.NEON_STORAGE_ACCESS_KEY || process.env.AWS_ACCESS_KEY_ID
const secretAccessKey = process.env.NEON_STORAGE_SECRET_KEY || process.env.AWS_SECRET_ACCESS_KEY
const region = process.env.NEON_STORAGE_REGION || process.env.AWS_REGION || 'auto'
const bucketName = process.env.AWS_BUCKET_NAME || process.env.NEON_STORAGE_BUCKET || 'executablefiles'

if (!endpoint || !accessKeyId || !secretAccessKey) {
  console.warn('⚠️ Neon Object Storage credentials (NEON_STORAGE_ENDPOINT, NEON_STORAGE_ACCESS_KEY, NEON_STORAGE_SECRET_KEY) are not fully set in .env')
}

const s3Client = new S3Client({
  endpoint,
  region,
  credentials: {
    accessKeyId,
    secretAccessKey,
  },
  forcePathStyle: true,
})

/**
 * Upload a file buffer directly to Neon Object Storage
 * @param {Buffer} fileBuffer
 * @param {string} originalName
 * @param {string} mimeType
 * @returns {Promise<{ key: string, location: string }>}
 */
async function uploadFile(fileBuffer, originalName, mimeType = 'application/octet-stream') {
  if (!endpoint || !accessKeyId || !secretAccessKey) {
    throw new Error('Neon Object Storage is not configured. Please check NEON_STORAGE credentials in .env.')
  }

  const ext = path.extname(originalName)
  const baseName = path.basename(originalName, ext).replace(/[^a-zA-Z0-9_-]/g, '_')
  const uniqueKey = `jobs/${Date.now()}-${Math.round(Math.random() * 1e6)}-${baseName}${ext}`

  const command = new PutObjectCommand({
    Bucket: bucketName,
    Key: uniqueKey,
    Body: fileBuffer,
    ContentType: mimeType,
  })

  await s3Client.send(command)

  return {
    key: uniqueKey,
    location: `${endpoint}/${bucketName}/${uniqueKey}`,
  }
}

/**
 * Download / Stream a file from Neon Object Storage
 * @param {string} key
 * @returns {Promise<any>}
 */
async function getFileStream(key) {
  if (!endpoint || !accessKeyId || !secretAccessKey) {
    throw new Error('Neon Object Storage is not configured.')
  }

  const command = new GetObjectCommand({
    Bucket: bucketName,
    Key: key,
  })

  const response = await s3Client.send(command)
  return response.Body
}

/**
 * Delete a file from Neon Object Storage
 * @param {string} key
 */
async function deleteFile(key) {
  if (!endpoint || !accessKeyId || !secretAccessKey) return

  const command = new DeleteObjectCommand({
    Bucket: bucketName,
    Key: key,
  })

  await s3Client.send(command)
}

module.exports = {
  uploadFile,
  getFileStream,
  deleteFile,
  s3Client,
}
