import { useState, useRef } from 'react'
import { useNavigate } from 'react-router-dom'
import { Upload, FileCode, Plus, Trash2, ChevronRight } from 'lucide-react'
import { submitJob } from '../api/client'

const ACCEPTED_TYPES = '.sh,.py,.exe,.c,.cpp,.bat,.cmd,.js'
const ACCEPTED_DISPLAY = '.sh, .py, .exe, .c,.cpp,.bat,.cmd,.js'

export default function JobSubmit() {
  const navigate = useNavigate()
  const fileInputRef = useRef(null)

  /* Form state */
  const [file, setFile] = useState(null)
  const [dragOver, setDragOver] = useState(false)
  const [name, setName] = useState('')
  const [args, setArgs] = useState('')
  const [envPairs, setEnvPairs] = useState([{ key: '', value: '' }])
  const [step, setStep] = useState(1)  // 1: upload, 2: configure
  const [submitting, setSubmitting] = useState(false)
  const [error, setError] = useState(null)

  /* File handlers */
  const handleFile = (f) => {
    if (!f) return
    setFile(f)
    if (!name) setName(f.name.replace(/\.[^/.]+$/, ''))
  }

  const handleDrop = (e) => {
    e.preventDefault()
    setDragOver(false)
    const f = e.dataTransfer.files?.[0]
    if (f) handleFile(f)
  }

  /* Env var helpers */
  const addEnvRow = () => setEnvPairs((p) => [...p, { key: '', value: '' }])
  const removeEnvRow = (i) => setEnvPairs((p) => p.filter((_, idx) => idx !== i))
  const updateEnvRow = (i, field, val) =>
    setEnvPairs((p) => p.map((row, idx) => (idx === i ? { ...row, [field]: val } : row)))

  /* Submit */
  const handleSubmit = async (e) => {
    e.preventDefault()
    if (!file) return
    setSubmitting(true)
    setError(null)

    try {
      const formData = new FormData()
      formData.append('file', file)
      formData.append('name', name || file.name)
      if (args.trim()) formData.append('args', args.trim())

      const env = {}
      envPairs.forEach(({ key, value }) => { if (key.trim()) env[key.trim()] = value })
      if (Object.keys(env).length > 0) {
        formData.append('env', JSON.stringify(env))
      }

      const result = await submitJob(formData)
      navigate(`/jobs/${result.id ?? result.job?.id}`)
    } catch (err) {
      setError(err.message)
      setSubmitting(false)
    }
  }

  const canProceed = file !== null

  return (
    <div style={{ maxWidth: 680, margin: '0 auto' }}>
      {/* Header */}
      <div className="page-header">
        <h1>Submit Job</h1>
        <p>Upload an executable and configure its runtime parameters</p>
      </div>

      {/* Step indicator */}
      <div className="flex items-center gap-3 mb-8">
        {[1, 2].map((s) => (
          <div key={s} className="flex items-center gap-2">
            <div style={{
              width: 28,
              height: 28,
              borderRadius: '50%',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              fontSize: '0.8rem',
              fontWeight: 700,
              background: step >= s ? 'var(--accent)' : 'var(--bg-elevated)',
              color: step >= s ? '#fff' : 'var(--text-muted)',
              border: step >= s ? '2px solid var(--accent)' : '2px solid var(--border)',
              transition: 'all 0.2s',
            }}>
              {s}
            </div>
            <span style={{
              fontSize: '0.8rem',
              fontWeight: 500,
              color: step >= s ? 'var(--text-primary)' : 'var(--text-muted)',
            }}>
              {s === 1 ? 'Upload File' : 'Configure'}
            </span>
            {s < 2 && (
              <ChevronRight size={14} style={{ color: 'var(--text-muted)' }} />
            )}
          </div>
        ))}
      </div>

      <form onSubmit={handleSubmit}>
        {/* ── STEP 1: Upload ── */}
        {step === 1 && (
          <div className="card">
            <h3 className="mb-6">Upload Executable</h3>

            {file ? (
              /* File selected display */
              <div className="file-selected mb-4">
                <FileCode size={18} color="var(--accent)" />
                <div className="flex-1 min-w-0">
                  <div className="truncate font-medium" style={{ fontSize: '0.875rem' }}>
                    {file.name}
                  </div>
                  <div className="text-xs text-muted">
                    {(file.size / 1024).toFixed(1)} KB
                  </div>
                </div>
                <button
                  type="button"
                  className="btn btn-ghost btn-sm"
                  onClick={() => { setFile(null); setName('') }}
                >
                  <Trash2 size={14} />
                  Remove
                </button>
              </div>
            ) : (
              /* Drop zone */
              <div
                className={`file-drop-zone${dragOver ? ' drag-over' : ''} mb-4`}
                onDragOver={(e) => { e.preventDefault(); setDragOver(true) }}
                onDragLeave={() => setDragOver(false)}
                onDrop={handleDrop}
                onClick={() => fileInputRef.current?.click()}
              >
                <input
                  ref={fileInputRef}
                  type="file"
                  accept={ACCEPTED_TYPES}
                  onChange={(e) => handleFile(e.target.files?.[0])}
                  style={{ display: 'none' }}
                />
                <div className="file-drop-zone-icon">
                  <Upload size={40} />
                </div>
                <p className="file-drop-zone-text">
                  <span style={{ color: 'var(--accent)', fontWeight: 600 }}>
                    Click to upload
                  </span>{' '}
                  or drag and drop
                </p>
                <p className="file-drop-zone-hint">
                  Accepted: {ACCEPTED_DISPLAY}
                </p>
              </div>
            )}

            <div className="flex justify-end">
              <button
                type="button"
                className="btn btn-primary"
                disabled={!canProceed}
                onClick={() => setStep(2)}
              >
                Continue
                <ChevronRight size={16} />
              </button>
            </div>
          </div>
        )}

        {/* ── STEP 2: Configure ── */}
        {step === 2 && (
          <div className="flex flex-col gap-5">
            <div className="card">
              <h3 className="mb-5">Job Configuration</h3>

              {/* Job Name */}
              <div className="form-group mb-5">
                <label className="form-label required">Job Name</label>
                <input
                  className="form-input"
                  type="text"
                  placeholder="e.g. data-processing-run-1"
                  value={name}
                  onChange={(e) => setName(e.target.value)}
                  required
                />
              </div>

              {/* Args */}
              <div className="form-group mb-5">
                <label className="form-label">Arguments</label>
                <input
                  className="form-input"
                  type="text"
                  placeholder="e.g. --input data.csv --verbose"
                  value={args}
                  onChange={(e) => setArgs(e.target.value)}
                />
                <span className="form-hint">
                  Command-line arguments passed to the executable
                </span>
              </div>

              {/* Environment Variables */}
              <div className="form-group">
                <label className="form-label">Environment Variables</label>
                <div className="kv-list">
                  {envPairs.map((row, i) => (
                    <div key={i} className="kv-row">
                      <input
                        className="form-input"
                        type="text"
                        placeholder="KEY"
                        value={row.key}
                        onChange={(e) => updateEnvRow(i, 'key', e.target.value)}
                        style={{ fontFamily: 'var(--font-mono)', fontSize: '0.8rem' }}
                      />
                      <input
                        className="form-input"
                        type="text"
                        placeholder="value"
                        value={row.value}
                        onChange={(e) => updateEnvRow(i, 'value', e.target.value)}
                        style={{ fontFamily: 'var(--font-mono)', fontSize: '0.8rem' }}
                      />
                      <button
                        type="button"
                        className="btn btn-ghost btn-icon"
                        onClick={() => removeEnvRow(i)}
                        disabled={envPairs.length === 1}
                        title="Remove"
                      >
                        <Trash2 size={14} />
                      </button>
                    </div>
                  ))}
                </div>
                <button
                  type="button"
                  className="btn btn-ghost btn-sm mt-2"
                  onClick={addEnvRow}
                  style={{ alignSelf: 'flex-start' }}
                >
                  <Plus size={14} />
                  Add variable
                </button>
              </div>
            </div>

            {/* Selected file summary */}
            <div className="card" style={{ padding: 'var(--space-4) var(--space-5)' }}>
              <div className="flex items-center gap-3">
                <FileCode size={16} color="var(--accent)" />
                <div>
                  <div className="text-sm font-medium">{file?.name}</div>
                  <div className="text-xs text-muted">
                    {(file?.size / 1024).toFixed(1)} KB
                  </div>
                </div>
                <button
                  type="button"
                  className="btn btn-ghost btn-sm"
                  style={{ marginLeft: 'auto' }}
                  onClick={() => setStep(1)}
                >
                  Change file
                </button>
              </div>
            </div>

            {error && (
              <div className="card" style={{ borderColor: 'var(--danger)', background: 'var(--danger-dim)' }}>
                <p className="text-sm" style={{ color: 'var(--danger)' }}>{error}</p>
              </div>
            )}

            {/* Actions */}
            <div className="flex gap-3 justify-end">
              <button
                type="button"
                className="btn btn-secondary"
                onClick={() => setStep(1)}
                disabled={submitting}
              >
                Back
              </button>
              <button
                type="submit"
                className="btn btn-primary"
                disabled={submitting || !name.trim()}
              >
                {submitting ? (
                  <>
                    <div className="spinner" style={{ width: 14, height: 14, borderWidth: 2 }} />
                    Submitting…
                  </>
                ) : (
                  <>
                    <Upload size={16} />
                    Submit Job
                  </>
                )}
              </button>
            </div>
          </div>
        )}
      </form>
    </div>
  )
}
