import { useRef, useEffect, useState } from 'react'
import { Terminal, Copy, Check, ArrowDownCircle, PauseCircle } from 'lucide-react'

function classifyLine(line) {
  const l = line.toLowerCase()
  if (/\b(error|err|fail|fatal|exception)\b/.test(l)) return 'log-error'
  if (/\b(warn|warning)\b/.test(l))                   return 'log-warn'
  if (/\b(success|done|completed|ok)\b/.test(l))      return 'log-success'
  return 'log-info'
}

/**
 * LogViewer
 * @param {{
 *   lines: Array<{ line: string, level?: string }>,
 *   streaming?: boolean,
 * }} props
 */
export default function LogViewer({ lines = [], streaming = false }) {
  const bottomRef  = useRef(null)
  const contentRef = useRef(null)
  const [pinned,   setPinned]  = useState(true) // auto-scroll
  const [copied,   setCopied]  = useState(false)

  /* Auto-scroll when pinned */
  useEffect(() => {
    if (pinned && bottomRef.current) {
      bottomRef.current.scrollIntoView({ behavior: 'smooth' })
    }
  }, [lines, pinned])

  /* Detect manual scroll-up → unpin */
  const handleScroll = () => {
    if (!contentRef.current) return
    const { scrollTop, scrollHeight, clientHeight } = contentRef.current
    setPinned(scrollHeight - scrollTop - clientHeight < 40)
  }

  const copyAll = () => {
    const text = lines.map((l) => l.line ?? l).join('\n')
    navigator.clipboard.writeText(text).then(() => {
      setCopied(true)
      setTimeout(() => setCopied(false), 2000)
    })
  }

  return (
    <div className="log-viewer">
      {/* Toolbar */}
      <div className="log-toolbar">
        <div className="log-toolbar-title">
          <Terminal size={13} />
          Output Log
          {streaming && (
            <span style={{
              display: 'inline-flex',
              alignItems: 'center',
              gap: 4,
              color: 'var(--success)',
              fontSize: '0.7rem',
            }}>
              <span className="status-dot connected" />
              Streaming
            </span>
          )}
          <span style={{ color: 'var(--text-muted)', fontWeight: 400, fontSize: '0.7rem' }}>
            {lines.length} line{lines.length !== 1 ? 's' : ''}
          </span>
        </div>
        <div className="flex gap-2">
          <button
            className={`btn btn-ghost btn-sm btn-icon`}
            onClick={() => setPinned((p) => !p)}
            title={pinned ? 'Pause auto-scroll' : 'Resume auto-scroll'}
            style={{ color: pinned ? 'var(--accent)' : 'var(--text-muted)' }}
          >
            {pinned ? <PauseCircle size={15} /> : <ArrowDownCircle size={15} />}
          </button>
          <button
            className="btn btn-ghost btn-sm btn-icon"
            onClick={copyAll}
            title="Copy all"
          >
            {copied ? <Check size={15} color="var(--success)" /> : <Copy size={15} />}
          </button>
        </div>
      </div>

      {/* Log lines */}
      <div className="log-content" ref={contentRef} onScroll={handleScroll}>
        {lines.length === 0 ? (
          <span style={{ color: 'var(--text-muted)' }}>
            {streaming ? 'Waiting for output…' : 'No output available.'}
          </span>
        ) : (
          lines.map((entry, i) => {
            const text  = typeof entry === 'string' ? entry : (entry.line ?? '')
            const level = entry.level ?? classifyLine(text)
            return (
              <div key={i} className="log-line">
                <span className="log-line-num">{i + 1}</span>
                <span className={`log-line-text ${level}`}>{text}</span>
              </div>
            )
          })
        )}
        <div ref={bottomRef} />
      </div>
    </div>
  )
}
