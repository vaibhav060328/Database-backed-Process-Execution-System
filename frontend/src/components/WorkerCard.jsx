import { Link } from 'react-router-dom'
import { Clock, Cpu, MemoryStick } from 'lucide-react'

function timeAgo(ts) {
  if (!ts) return 'never'
  const diff = Math.floor((Date.now() - new Date(ts)) / 1000)
  if (diff < 60)  return `${diff}s ago`
  if (diff < 3600) return `${Math.floor(diff / 60)}m ago`
  return `${Math.floor(diff / 3600)}h ago`
}

function barColor(pct) {
  if (pct > 85) return 'high'
  if (pct > 60) return 'medium'
  return ''
}

/**
 * WorkerCard
 * @param {{ worker: {
 *   id: string,
 *   status: 'online'|'busy'|'offline',
 *   cpu: number,
 *   memory: number,
 *   currentJobId?: string|number,
 *   lastHeartbeat?: string,
 * } }} props
 */
export default function WorkerCard({ worker }) {
  const { id, status, cpu = 0, memory = 0, currentJobId, lastHeartbeat } = worker

  return (
    <div className={`worker-card ${status ?? 'offline'}`}>
      {/* Header */}
      <div className="flex items-center justify-between mb-4">
        <div>
          <div className="flex items-center gap-2">
            <Cpu size={16} color="var(--text-muted)" />
            <span className="font-semibold text-sm">{id}</span>
          </div>
          <div className="text-xs text-muted mt-1">
            <Clock size={11} style={{ display: 'inline', marginRight: 4 }} />
            {timeAgo(lastHeartbeat)}
          </div>
        </div>
        <span style={{
          display: 'inline-flex',
          alignItems: 'center',
          gap: 6,
          padding: '3px 10px',
          borderRadius: 'var(--radius-full)',
          fontSize: '0.7rem',
          fontWeight: 600,
          textTransform: 'uppercase',
          letterSpacing: '0.04em',
          background:
            status === 'online'  ? 'var(--success-dim)' :
            status === 'busy'    ? 'var(--warning-dim)' :
            'var(--neutral-dim)',
          color:
            status === 'online'  ? 'var(--success)' :
            status === 'busy'    ? 'var(--warning)' :
            'var(--neutral)',
        }}>
          <span className={`status-dot ${status === 'online' || status === 'busy' ? 'connected' : 'disconnected'}`}
            style={{ width: 6, height: 6 }} />
          {status ?? 'offline'}
        </span>
      </div>

      {/* Current Job */}
      {currentJobId ? (
        <div style={{
          background: 'var(--bg-elevated)',
          borderRadius: 'var(--radius-md)',
          padding: '6px 12px',
          fontSize: '0.78rem',
          marginBottom: 'var(--space-4)',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
        }}>
          <span className="text-muted">Running job</span>
          <Link
            to={`/jobs/${currentJobId}`}
            style={{ color: 'var(--accent)', fontWeight: 600, fontFamily: 'var(--font-mono)' }}
          >
            #{currentJobId}
          </Link>
        </div>
      ) : (
        <div style={{
          background: 'var(--bg-elevated)',
          borderRadius: 'var(--radius-md)',
          padding: '6px 12px',
          fontSize: '0.78rem',
          marginBottom: 'var(--space-4)',
          color: 'var(--text-muted)',
        }}>
          No active job
        </div>
      )}

      {/* Metrics */}
      <div className="metric-bar-wrap">
        {/* CPU */}
        <div className="metric-row">
          <div className="metric-label">
            <span className="flex items-center gap-1">
              <Cpu size={11} />
              CPU
            </span>
            <span>{cpu.toFixed(1)}%</span>
          </div>
          <div className="metric-bar">
            <div
              className={`metric-bar-fill ${barColor(cpu)}`}
              style={{ width: `${Math.min(cpu, 100)}%`, background: cpu > 85 ? 'var(--danger)' : cpu > 60 ? 'var(--warning)' : 'var(--accent)' }}
            />
          </div>
        </div>

        {/* Memory */}
        <div className="metric-row">
          <div className="metric-label">
            <span className="flex items-center gap-1">
              <MemoryStick size={11} />
              Memory
            </span>
            <span>{memory.toFixed(1)}%</span>
          </div>
          <div className="metric-bar">
            <div
              className={`metric-bar-fill ${barColor(memory)}`}
              style={{ width: `${Math.min(memory, 100)}%`, background: memory > 85 ? 'var(--danger)' : memory > 60 ? 'var(--warning)' : 'var(--info)' }}
            />
          </div>
        </div>
      </div>
    </div>
  )
}
