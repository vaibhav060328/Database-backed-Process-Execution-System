import { useState, useEffect, useCallback } from 'react'
import { useParams, useNavigate, Link } from 'react-router-dom'
import {
  ArrowLeft, Calendar, Clock, Server, FileCode, XCircle, AlertTriangle,
} from 'lucide-react'
import { getJob, cancelJob } from '../api/client'
import { useJobLog, useJobStatus } from '../hooks/useSocket'
import StatusBadge from '../components/StatusBadge'
import LogViewer from '../components/LogViewer'

function formatDate(ts) {
  if (!ts) return '—'
  return new Date(ts).toLocaleString()
}

function formatDuration(startedAt, finishedAt) {
  if (!startedAt) return '—'
  const end = finishedAt ? new Date(finishedAt) : new Date()
  const s = Math.floor((end - new Date(startedAt)) / 1000)
  if (s < 60) return `${s}s`
  if (s < 3600) return `${Math.floor(s/60)}m ${s%60}s`
  return `${Math.floor(s/3600)}h ${Math.floor((s%3600)/60)}m`
}

export default function JobDetail() {
  const { id } = useParams()
  const navigate = useNavigate()

  const [job,       setJob]       = useState(null)
  const [logs,      setLogs]      = useState([])
  const [loading,   setLoading]   = useState(true)
  const [cancelling, setCancelling] = useState(false)
  const [error,     setError]     = useState(null)

  /* Fetch job */
  useEffect(() => {
    setLoading(true)
    setLogs([])
    getJob(id)
      .then((data) => { setJob(data); setLoading(false) })
      .catch((err) => { setError(err.message); setLoading(false) })
  }, [id])

  /* Real-time status update */
  const handleStatus = useCallback((payload) => {
    if (String(payload.id) === String(id)) {
      setJob((prev) => prev ? { ...prev, status: payload.status, ...payload } : prev)
    }
  }, [id])
  useJobStatus(handleStatus)

  /* Live log streaming */
  const handleLog = useCallback((payload) => {
    setLogs((prev) => [...prev, payload])
  }, [])
  useJobLog(id, handleLog)

  /* Cancel job */
  const handleCancel = async () => {
    if (!window.confirm('Cancel this job?')) return
    setCancelling(true)
    try {
      await cancelJob(id)
      setJob((prev) => prev ? { ...prev, status: 'cancelled' } : prev)
    } catch (err) {
      setError(err.message)
    } finally {
      setCancelling(false)
    }
  }

  const isActive = job?.status === 'running' || job?.status === 'queued'

  /* ── Loading ── */
  if (loading) {
    return (
      <div className="loading-overlay" style={{ minHeight: 400 }}>
        <div className="spinner" style={{ width: 32, height: 32, borderWidth: 3 }} />
      </div>
    )
  }

  /* ── Error ── */
  if (error && !job) {
    return (
      <div className="card" style={{ textAlign: 'center', padding: 'var(--space-12)' }}>
        <AlertTriangle size={40} color="var(--danger)" style={{ marginBottom: 'var(--space-4)' }} />
        <h3>Failed to load job</h3>
        <p className="mt-2">{error}</p>
        <button className="btn btn-secondary mt-6" onClick={() => navigate('/jobs')}>
          Back to Jobs
        </button>
      </div>
    )
  }

  /* ── Env vars ── */
  let envEntries = []
  if (job?.env) {
    try {
      const parsed = typeof job.env === 'string' ? JSON.parse(job.env) : job.env
      envEntries = Object.entries(parsed)
    } catch { /* ignore */ }
  }

  return (
    <div>
      {/* Back */}
      <div className="mb-6">
        <Link to="/jobs" className="btn btn-ghost btn-sm" style={{ paddingLeft: 0 }}>
          <ArrowLeft size={15} />
          All Jobs
        </Link>
      </div>

      {/* Header */}
      <div className="flex items-start justify-between mb-8 gap-4" style={{ flexWrap: 'wrap' }}>
        <div>
          <div className="flex items-center gap-3 mb-2">
            <h1 style={{ margin: 0 }}>{job?.name ?? 'Unnamed Job'}</h1>
            <StatusBadge status={job?.status} />
          </div>
          <span style={{
            fontFamily: 'var(--font-mono)',
            fontSize: '0.75rem',
            color: 'var(--text-muted)',
          }}>
            Job #{id}
          </span>
        </div>

        {isActive && (
          <button
            className="btn btn-danger"
            onClick={handleCancel}
            disabled={cancelling}
          >
            <XCircle size={15} />
            {cancelling ? 'Cancelling…' : 'Cancel Job'}
          </button>
        )}
      </div>

      {/* Info grid */}
      <div className="grid-2 gap-5 mb-6">
        {/* Timeline */}
        <div className="card">
          <h3 className="mb-5" style={{ fontSize: '0.9rem' }}>Timeline</h3>
          <div className="flex flex-col gap-4">
            {[
              { icon: Calendar, label: 'Submitted', value: formatDate(job?.createdAt) },
              { icon: Clock,    label: 'Started',   value: formatDate(job?.startedAt) },
              { icon: Clock,    label: 'Finished',  value: formatDate(job?.finishedAt) },
              { icon: Clock,    label: 'Duration',  value: formatDuration(job?.startedAt, job?.finishedAt) },
            ].map(({ icon: Icon, label, value }) => (
              <div key={label} className="flex items-center gap-3">
                <Icon size={14} color="var(--text-muted)" />
                <span className="text-xs text-muted" style={{ minWidth: 72 }}>{label}</span>
                <span className="text-sm font-mono">{value}</span>
              </div>
            ))}
          </div>
        </div>

        {/* Metadata */}
        <div className="card">
          <h3 className="mb-5" style={{ fontSize: '0.9rem' }}>Metadata</h3>
          <div className="flex flex-col gap-4">
            {job?.fileName && (
              <div className="flex items-center gap-3">
                <FileCode size={14} color="var(--text-muted)" />
                <span className="text-xs text-muted" style={{ minWidth: 72 }}>File</span>
                <span className="text-sm font-mono truncate">{job.fileName}</span>
              </div>
            )}
            {job?.workerId && (
              <div className="flex items-center gap-3">
                <Server size={14} color="var(--text-muted)" />
                <span className="text-xs text-muted" style={{ minWidth: 72 }}>Worker</span>
                <Link
                  to="/workers"
                  className="text-sm"
                  style={{ color: 'var(--accent)' }}
                >
                  {job.workerId}
                </Link>
              </div>
            )}
            {job?.args && (
              <div className="flex flex-col gap-1">
                <span className="text-xs text-muted">Arguments</span>
                <code style={{
                  fontSize: '0.78rem',
                  background: 'var(--bg-elevated)',
                  borderRadius: 'var(--radius-sm)',
                  padding: '6px 10px',
                  color: 'var(--text-secondary)',
                  wordBreak: 'break-all',
                }}>
                  {job.args}
                </code>
              </div>
            )}
            {envEntries.length > 0 && (
              <div className="flex flex-col gap-2">
                <span className="text-xs text-muted">Environment Variables</span>
                <div className="flex flex-col gap-1">
                  {envEntries.map(([k, v]) => (
                    <div key={k} style={{
                      display: 'flex',
                      gap: 8,
                      fontSize: '0.78rem',
                      fontFamily: 'var(--font-mono)',
                      background: 'var(--bg-elevated)',
                      borderRadius: 'var(--radius-sm)',
                      padding: '4px 10px',
                    }}>
                      <span style={{ color: 'var(--accent)' }}>{k}</span>
                      <span style={{ color: 'var(--text-muted)' }}>=</span>
                      <span style={{ color: 'var(--text-secondary)' }}>{v}</span>
                    </div>
                  ))}
                </div>
              </div>
            )}
          </div>
        </div>
      </div>

      {/* Log Viewer */}
      <LogViewer lines={logs} streaming={isActive} />
    </div>
  )
}
