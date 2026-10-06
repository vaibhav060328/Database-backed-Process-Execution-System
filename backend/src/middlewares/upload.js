const multer = require('multer')
const path = require('path')

// Use in-memory storage so uploaded bytes can be piped directly to Neon Object Storage
const storage = multer.memoryStorage()

// Allow common executable and script file types
const fileFilter = (req, file, cb) => {
  const allowedExtensions = ['.sh', '.py', '.exe', '.c', '.cpp', '.bat', '.cmd', '.js', '.ps1']
  const ext = path.extname(file.originalname).toLowerCase()

  if (allowedExtensions.includes(ext) || !ext) {
    cb(null, true)
  } else {
    cb(new Error(`Unsupported file type '${ext}'. Allowed types: ${allowedExtensions.join(', ')}`), false)
  }
}

const upload = multer({
  storage,
  fileFilter,
  limits: {
    fileSize: 100 * 1024 * 1024, // 100 MB max
  },
})

module.exports = {
  upload,
}
