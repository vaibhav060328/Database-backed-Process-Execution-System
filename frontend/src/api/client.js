import axios from 'axios'

const BASE_URL = 'http://localhost:3000'

const client = axios.create({
  baseURL: BASE_URL,
  timeout: 15000,
  headers: { 'Content-Type': 'application/json' },
})

/* ── Response interceptor ──────────────────────────────────── */
client.interceptors.response.use(
  (res) => res,
  (err) => {
    const message =
      err.response?.data?.message || err.message || 'An unexpected error occurred'
    console.error('[API Error]', message)
    return Promise.reject(new Error(message))
  }
)

/* ── Dashboard ─────────────────────────────────────────────── */
export const getDashboardStats = (range = 'minutely') =>
  client.get(`/api/dashboard?range=${range}`).then((r) => r.data)

/* ── Jobs ───────────────────────────────────────────────────── */
/**
 * @param {Object} params
 * @param {string}  [params.status]  - filter by status
 * @param {string}  [params.search]  - search by job name
 * @param {number}  [params.page=1]
 * @param {number}  [params.limit=10]
 */
export const getJobs = (params = {}) =>
  client.get('/api/jobs', { params: { page: 1, limit: 10, ...params } }).then((r) => r.data)

export const getJob = (id) =>
  client.get(`/api/jobs/${id}`).then((r) => r.data)

/**
 * Submit a new job (multipart/form-data)
 * Fields: file (.sh/.py/.exe/.c), name, args, env (JSON)
 */
export const submitJob = (formData) =>
  client.post('/api/jobs', formData, {
    headers: { 'Content-Type': 'multipart/form-data' },
  }).then((r) => r.data)

export const cancelJob = (id) =>
  client.delete(`/api/jobs/${id}`).then((r) => r.data)

/* ── Workers ────────────────────────────────────────────────── */
export const getWorkers = () =>
  client.get('/api/workers').then((r) => r.data)

export default client
