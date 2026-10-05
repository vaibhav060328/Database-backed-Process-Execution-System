import { useState, useEffect, useCallback } from 'react'
import { RefreshCw, Server, Wifi, WifiOff } from 'lucide-react'
import { getWorkers } from '../api/client'
import { useWorkerUpdates } from '../hooks/useSocket'
import WorkerCard from '../components/WorkerCard'

export default function Workers() {
  const [workers, setWorkers] = useState([])
  const [loading, setLoading] = useState(true)
  const [refresh, setRefresh] = useState(0)

  /* Fetch workers */
  useEffect(() => {
    setLoading(true)
    getWorkers()
      .then((data) => {
        setWorkers(Array.isArray(data) ? data : data.workers ?? [])
        setLoading(false)
      })
      .catch(() => setLoading(false))
  }, [refresh])

  /* Real-time worker heartbeats — patch in-place */
  const handleWorkerUpdate = useCallback((payload) => {
    setWorkers((prev) =>
      prev.map((w) =>
        String(w.id) === String(payload.id) ? { ...w, ...payload } : w
      )
    )
  }, [])
  useWorkerUpdates(handleWorkerUpdate)

  /* Summary counts */
  const online  = workers.filter((w) => w.status === 'online').length
  const busy    = workers.filter((w) => w.status === 'busy').length
  const offline = workers.filter((w) => w.status === 'offline').length

  return (
    <div>
      {/* Header */}
      <div className="page-header flex items-center justify-between">
        <div>
          <h1>Workers</h1>
          <p>
            {workers.length} worker{workers.length !== 1 ? 's' : ''} registered
          </p>
        </div>
        <button
          className="btn btn-secondary btn-sm"
          onClick={() => setRefresh((r) => r + 1)}
        >
          <RefreshCw size={14} />
          Refresh
        </button>
      </div>

      {/* Summary badges */}
      {!loading && workers.length > 0 && (
        <div className="flex gap-3 mb-8" style={{ flexWrap: 'wrap' }}>
          <div style={{
            display: 'flex', alignItems: 'center', gap: 8,
            background: 'var(--success-dim)', color: 'var(--success)',
            borderRadius: 'var(--radius-full)',
            padding: '5px 14px', fontSize: '0.8rem', fontWeight: 600,
            border: '1px solid rgba(34,197,94,0.2)',
          }}>
            <Wifi size={13} />
            {online} Online
          </div>
          <div style={{
            display: 'flex', alignItems: 'center', gap: 8,
            background: 'var(--warning-dim)', color: 'var(--warning)',
            borderRadius: 'var(--radius-full)',
            padding: '5px 14px', fontSize: '0.8rem', fontWeight: 600,
            border: '1px solid rgba(245,158,11,0.2)',
          }}>
            <Server size={13} />
            {busy} Busy
          </div>
          <div style={{
            display: 'flex', alignItems: 'center', gap: 8,
            background: 'var(--neutral-dim)', color: 'var(--neutral)',
            borderRadius: 'var(--radius-full)',
            padding: '5px 14px', fontSize: '0.8rem', fontWeight: 600,
            border: '1px solid rgba(107,114,128,0.2)',
          }}>
            <WifiOff size={13} />
            {offline} Offline
          </div>
        </div>
      )}

      {/* Content */}
      {loading ? (
        <div className="loading-overlay"><div className="spinner" /></div>
      ) : workers.length === 0 ? (
        <div className="card">
          <div className="empty-state">
            <Server size={40} className="empty-state-icon" />
            <h3>No workers registered</h3>
            <p>Worker nodes will appear here once they connect to the system.</p>
          </div>
        </div>
      ) : (
        <div className="grid-auto-fill">
          {workers.map((worker) => (
            <WorkerCard key={worker.id} worker={worker} />
          ))}
        </div>
      )}
    </div>
  )
}
