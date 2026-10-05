import { useState, useEffect, useCallback, useRef } from 'react'
import { useNavigate } from 'react-router-dom'
import {
  Search, LayoutGrid, List as ListIcon, ChevronLeft, ChevronRight,
} from 'lucide-react'
import { getJobs } from '../api/client'
import { useJobStatus } from '../hooks/useSocket'
import StatusBadge from '../components/StatusBadge'
import JobCard from '../components/JobCard'

const STATUS_TABS = ['all', 'queued', 'running', 'done', 'failed', 'cancelled']
const PAGE_SIZES  = [10, 25, 50]

function formatDuration(startedAt, finishedAt) {
  if (!startedAt) return '—'
  const end = finishedAt ? new Date(finishedAt) : new Date()
  const s = Math.floor((end - new Date(startedAt)) / 1000)
  if (s < 60) return `${s}s`
  if (s < 3600) return `${Math.floor(s/60)}m ${s%60}s`
  return `${Math.floor(s/3600)}h ${Math.floor((s%3600)/60)}m`
}

export default function JobList() {
  const navigate = useNavigate()
  const searchRef = useRef(null)

  const [jobs,      setJobs]      = useState([])
  const [total,     setTotal]     = useState(0)
  const [loading,   setLoading]   = useState(true)
  const [page,      setPage]      = useState(1)
  const [limit,     setLimit]     = useState(10)
  const [search,    setSearch]    = useState('')
  const [status,    setStatus]    = useState('all')
  const [viewMode,  setViewMode]  = useState('table') // 'table' | 'grid'

  /* Debounced search */
  const [debouncedSearch, setDebouncedSearch] = useState('')
  useEffect(() => {
    const t = setTimeout(() => setDebouncedSearch(search), 350)
    return () => clearTimeout(t)
  }, [search])

  /* Fetch jobs */
  useEffect(() => {
    setLoading(true)
    const params = {
      page,
      limit,
      ...(status !== 'all' && { status }),
      ...(debouncedSearch && { search: debouncedSearch }),
    }
    getJobs(params)
      .then((data) => {
        setJobs(data.jobs ?? data)
        setTotal(data.total ?? (data.jobs ?? data).length)
        setLoading(false)
      })
      .catch(() => setLoading(false))
  }, [page, limit, status, debouncedSearch])

  /* Real-time status patch */
  const handleJobStatus = useCallback((payload) => {
    setJobs((prev) =>
      prev.map((j) =>
        String(j.id) === String(payload.id) ? { ...j, status: payload.status } : j
      )
    )
  }, [])
  useJobStatus(handleJobStatus)

  const totalPages = Math.ceil(total / limit) || 1

  return (
    <div>
      {/* Header */}
      <div className="page-header flex items-center justify-between">
        <div>
          <h1>Jobs</h1>
          <p>{total} job{total !== 1 ? 's' : ''} total</p>
        </div>
        <div className="flex gap-2">
          <button
            className={`btn btn-ghost btn-icon${viewMode === 'table' ? ' active' : ''}`}
            onClick={() => setViewMode('table')}
            title="Table view"
            style={{ color: viewMode === 'table' ? 'var(--accent)' : undefined }}
          >
            <ListIcon size={16} />
          </button>
          <button
            className={`btn btn-ghost btn-icon${viewMode === 'grid' ? ' active' : ''}`}
            onClick={() => setViewMode('grid')}
            title="Grid view"
            style={{ color: viewMode === 'grid' ? 'var(--accent)' : undefined }}
          >
            <LayoutGrid size={16} />
          </button>
        </div>
      </div>

      {/* Filters */}
      <div className="filter-bar">
        <div className="search-box" style={{ maxWidth: 300 }}>
          <Search className="search-box-icon" />
          <input
            ref={searchRef}
            className="form-input"
            placeholder="Search jobs…"
            value={search}
            onChange={(e) => { setSearch(e.target.value); setPage(1) }}
          />
        </div>

        <div className="segmented">
          {STATUS_TABS.map((s) => (
            <button
              key={s}
              className={`segmented-btn${status === s ? ' active' : ''}`}
              onClick={() => { setStatus(s); setPage(1) }}
              style={{ textTransform: 'capitalize' }}
            >
              {s === 'all' ? 'All' : s}
            </button>
          ))}
        </div>

        <div className="flex items-center gap-2 text-xs text-muted" style={{ marginLeft: 'auto' }}>
          Per page:
          <select
            className="form-select"
            value={limit}
            onChange={(e) => { setLimit(Number(e.target.value)); setPage(1) }}
            style={{ width: 72, padding: '4px 28px 4px 10px' }}
          >
            {PAGE_SIZES.map((s) => <option key={s} value={s}>{s}</option>)}
          </select>
        </div>
      </div>

      {/* Content */}
      {loading ? (
        <div className="loading-overlay"><div className="spinner" /></div>
      ) : jobs.length === 0 ? (
        <div className="card">
          <div className="empty-state">
            <ListIcon size={40} className="empty-state-icon" />
            <h3>No jobs found</h3>
            <p>Try adjusting your filters or submit a new job.</p>
          </div>
        </div>
      ) : viewMode === 'grid' ? (
        <div className="grid-auto-fill">
          {jobs.map((job) => <JobCard key={job.id} job={job} />)}
        </div>
      ) : (
        /* Table view */
        <div className="card" style={{ padding: 0 }}>
          <div className="table-wrapper" style={{ border: 'none', borderRadius: 'var(--radius-lg)' }}>
            <table>
              <thead>
                <tr>
                  <th>ID</th>
                  <th>Name</th>
                  <th>Status</th>
                  <th>Worker</th>
                  <th>Submitted</th>
                  <th>Duration</th>
                  <th />
                </tr>
              </thead>
              <tbody>
                {jobs.map((job) => (
                  <tr key={job.id} onClick={() => navigate(`/jobs/${job.id}`)}>
                    <td>
                      <span className="font-mono text-xs" style={{ color: 'var(--text-muted)' }}>
                        #{job.id}
                      </span>
                    </td>
                    <td style={{ fontWeight: 500, color: 'var(--text-primary)' }}>
                      {job.name ?? 'Unnamed'}
                    </td>
                    <td><StatusBadge status={job.status} /></td>
                    <td>{job.workerId ?? '—'}</td>
                    <td className="text-xs">
                      {job.createdAt ? new Date(job.createdAt).toLocaleString() : '—'}
                    </td>
                    <td className="font-mono text-xs">
                      {formatDuration(job.startedAt, job.finishedAt)}
                    </td>
                    <td>
                      <button
                        className="btn btn-ghost btn-sm"
                        onClick={(e) => { e.stopPropagation(); navigate(`/jobs/${job.id}`) }}
                      >
                        View
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>

            {/* Pagination */}
            <div className="pagination">
              <span>
                Showing {(page - 1) * limit + 1}–{Math.min(page * limit, total)} of {total}
              </span>
              <div className="pagination-controls">
                <button
                  className="pagination-btn"
                  disabled={page <= 1}
                  onClick={() => setPage((p) => p - 1)}
                >
                  <ChevronLeft size={14} />
                </button>
                {Array.from({ length: Math.min(totalPages, 5) }, (_, i) => {
                  const p = i + 1
                  return (
                    <button
                      key={p}
                      className={`pagination-btn${page === p ? ' active' : ''}`}
                      onClick={() => setPage(p)}
                    >
                      {p}
                    </button>
                  )
                })}
                {totalPages > 5 && page < totalPages && (
                  <>
                    <span style={{ color: 'var(--text-muted)' }}>…</span>
                    <button
                      className={`pagination-btn${page === totalPages ? ' active' : ''}`}
                      onClick={() => setPage(totalPages)}
                    >
                      {totalPages}
                    </button>
                  </>
                )}
                <button
                  className="pagination-btn"
                  disabled={page >= totalPages}
                  onClick={() => setPage((p) => p + 1)}
                >
                  <ChevronRight size={14} />
                </button>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}
