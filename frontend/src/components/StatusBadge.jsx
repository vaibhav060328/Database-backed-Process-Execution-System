import { Clock, Loader, CheckCircle, XCircle, MinusCircle } from 'lucide-react'

const STATUS_CONFIG = {
  queued: {
    label: 'Queued',
    className: 'badge badge-queued',
    Icon: Clock,
    spin: false,
  },
  running: {
    label: 'Running',
    className: 'badge badge-running',
    Icon: Loader,
    spin: true,
  },
  done: {
    label: 'Done',
    className: 'badge badge-done',
    Icon: CheckCircle,
    spin: false,
  },
  failed: {
    label: 'Failed',
    className: 'badge badge-failed',
    Icon: XCircle,
    spin: false,
  },
  cancelled: {
    label: 'Cancelled',
    className: 'badge badge-cancelled',
    Icon: MinusCircle,
    spin: false,
  },
}

/**
 * StatusBadge
 * @param {{ status: 'queued'|'running'|'done'|'failed'|'cancelled' }} props
 */
export default function StatusBadge({ status }) {
  const config = STATUS_CONFIG[status] ?? STATUS_CONFIG['queued']
  const { label, className, Icon, spin } = config

  return (
    <span className={className}>
      <Icon size={11} className={`badge-icon${spin ? ' spin' : ''}`} />
      {label}
    </span>
  )
}
