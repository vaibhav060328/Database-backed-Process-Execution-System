import { Link } from 'react-router-dom'
import { Clock, Server } from 'lucide-react'
import StatusBadge from './StatusBadge'

function formatDuration(startedAt, finishedAt) {
  if (!startedAt) return '—'
  const end = finishedAt ? new Date(finishedAt) : new Date()
  const ms  = end - new Date(startedAt)
  const s = Math.floor(ms / 1000)
  if (s < 60) return `${s}s`
  if (s < 3600) return `${Math.floor(s/60)}m ${s%60}s`
  return `${Math.floor(s/3600)}h ${Math.floor((s%3600)/60)}m`
}

/**
 * JobCard — compact card for a single job, used in the job list grid view.
 * @param {{ job: Object }} props
 */
export default function JobCard({ job }) {
  return (
    <Link
      to={`/jobs/${job.id}`}
      style={{ textDecoration: 'none' }}
    >
      <div className="card" style={{ cursor: 'pointer', transition: 'all 0.2s' }}
        onMouseEnter={(e) => e.currentTarget.style.borderColor = 'var(--accent-border)'}
        onMouseLeave={(e) => e.currentTarget.style.borderColor = 'var(--border)'}
      >
        {/* Header */}
        <div className="flex items-center justify-between mb-4">
          <span style={{
            fontSize: '0.7rem',
            fontWeight: 600,
            color: 'var(--text-muted)',
            fontFamily: 'var(--font-mono)',
          }}>
            #{job.id}
          </span>
          <StatusBadge status={job.status} />
        </div>

        {/* Job Name */}
        <h3 className="truncate" style={{ fontSize: '0.95rem', marginBottom: 'var(--space-3)' }}>
          {job.name ?? 'Unnamed Job'}
        </h3>

        {/* Meta */}
        <div className="flex flex-col gap-2">
          <div className="flex items-center gap-2 text-xs text-secondary">
            <Clock size={12} />
            <span>{job.createdAt ? new Date(job.createdAt).toLocaleString() : '—'}</span>
          </div>
          {job.workerId && (
            <div className="flex items-center gap-2 text-xs text-secondary">
              <Server size={12} />
              <span>Worker {job.workerId}</span>
            </div>
          )}
          <div className="text-xs text-muted" style={{ fontFamily: 'var(--font-mono)' }}>
            Duration: {formatDuration(job.startedAt, job.finishedAt)}
          </div>
        </div>
      </div>
    </Link>
  )
}
