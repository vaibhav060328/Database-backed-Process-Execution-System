import { useState, useEffect, useCallback } from 'react'
import { Link } from 'react-router-dom'
import {
  Activity, Layers, Play, XCircle, Server, Plus, RefreshCw,
} from 'lucide-react'
import { getDashboardStats } from '../api/client'
import { useJobStatus } from '../hooks/useSocket'
import StatsChart from '../components/StatsChart'
import StatusBadge from '../components/StatusBadge'

const RANGES = ['secondly', 'minutely', 'hourly', 'daily']

function timeAgo(ts) {
  if (!ts) return '—'
  const diff = Math.floor((Date.now() - new Date(ts)) / 1000)
  if (diff < 60)  return `${diff}s ago`
  if (diff < 3600) return `${Math.floor(diff/60)}m ago`
  return `${Math.floor(diff/3600)}h ago`
}

export default function Dashboard() {
  const [stats,    setStats]    = useState(null)
  const [range,    setRange]    = useState('minutely')
  const [loading,  setLoading]  = useState(true)
  const [activity, setActivity] = useState([])
  const [refresh,  setRefresh]  = useState(0)

  /* Fetch dashboard stats */
  useEffect(() => {
    setLoading(true)
    getDashboardStats(range)
      .then((data) => {
        setStats(data)
        setLoading(false)
      })
      .catch(() => setLoading(false))
  }, [range, refresh])

  /* Real-time activity via socket */
  const handleJobStatus = useCallback((payload) => {
    setActivity((prev) => [
      { ...payload, receivedAt: new Date().toISOString() },
      ...prev.slice(0, 9),
    ])
  }, [])

  useJobStatus(handleJobStatus)

  const statCards = stats
    ? [
        { label: 'Total Jobs',      value: stats.total   ?? 0, colorClass: 'accent',  icon: Layers },
        { label: 'Running',         value: stats.running ?? 0, colorClass: 'warning', icon: Play },
        { label: 'Failed',          value: stats.failed  ?? 0, colorClass: 'danger',  icon: XCircle },
        { label: 'Workers Online',  value: stats.workers ?? 0, colorClass: 'success', icon: Server },
      ]
    : []

  return (
    <div>
      {/* Header */}
      <div className="page-header flex items-center justify-between">
        <div>
          <h1>Dashboard</h1>
          <p>System overview and live activity</p>
        </div>
        <div className="flex gap-3">
          <button
            className="btn btn-secondary btn-sm"
            onClick={() => setRefresh((r) => r + 1)}
          >
            <RefreshCw size={14} />
            Refresh
          </button>
          <Link to="/jobs/new" className="btn btn-primary btn-sm">
            <Plus size={14} />
            New Job
          </Link>
        </div>
      </div>

      {/* Stat Cards */}
      <div className="grid-4 mb-8">
        {loading
          ? Array.from({ length: 4 }).map((_, i) => (
              <div key={i} className="stat-card" style={{ minHeight: 120 }}>
                <div className="loading-overlay" style={{ padding: 0, flex: 1 }}>
                  <div className="spinner" />
                </div>
              </div>
            ))
          : statCards.map(({ label, value, colorClass, icon: Icon }) => (
              <div key={label} className={`stat-card ${colorClass}`}>
                <div className={`stat-card-icon ${colorClass}`}>
                  <Icon size={18} />
                </div>
                <div>
                  <div className="stat-card-label">{label}</div>
                  <div className="stat-card-value">{value}</div>
                </div>
              </div>
            ))}
      </div>

      {/* Chart + Activity Feed */}
      <div className="grid-2 gap-6">
        {/* Jobs Over Time Chart */}
        <div className="card">
          <div className="flex items-center justify-between mb-6">
            <div className="flex items-center gap-2">
              <Activity size={16} color="var(--accent)" />
              <h3 style={{ margin: 0 }}>Jobs Over Time</h3>
            </div>
            <div className="segmented">
              {RANGES.map((r) => (
                <button
                  key={r}
                  className={`segmented-btn${range === r ? ' active' : ''}`}
                  onClick={() => setRange(r)}
                  style={{ textTransform: 'capitalize' }}
                >
                  {r.charAt(0).toUpperCase() + r.slice(1)}
                </button>
              ))}
            </div>
          </div>
          {loading ? (
            <div className="loading-overlay">
              <div className="spinner" />
            </div>
          ) : (
            <StatsChart data={stats?.chart ?? []} xKey="time" />
          )}
        </div>

        {/* Live Activity Feed */}
        <div className="card">
          <div className="flex items-center gap-2 mb-6">
            <div
              className="status-dot connected"
              style={{ width: 8, height: 8 }}
            />
            <h3 style={{ margin: 0 }}>Live Activity</h3>
          </div>

          {activity.length === 0 ? (
            <div className="empty-state" style={{ padding: 'var(--space-8)' }}>
              <div className="empty-state-icon">
                <Activity size={36} />
              </div>
              <p>Waiting for events…</p>
            </div>
          ) : (
            <div className="activity-feed">
              {activity.map((evt, i) => (
                <div key={i} className="activity-item">
                  <div
                    className="activity-dot"
                    style={{
                      background:
                        evt.status === 'done'    ? 'var(--success)' :
                        evt.status === 'failed'  ? 'var(--danger)'  :
                        evt.status === 'running' ? 'var(--warning)' :
                        'var(--info)',
                    }}
                  />
                  <div className="activity-body">
                    <div className="activity-text">
                      Job{' '}
                      <Link
                        to={`/jobs/${evt.id}`}
                        style={{ color: 'var(--accent)', fontWeight: 500 }}
                      >
                        #{evt.id}
                      </Link>{' '}
                      changed to{' '}
                      <strong>
                        <StatusBadge status={evt.status} />
                      </strong>
                    </div>
                    <div className="activity-time">{timeAgo(evt.receivedAt)}</div>
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  )
}
