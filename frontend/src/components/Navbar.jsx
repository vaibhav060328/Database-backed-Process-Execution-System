import { NavLink, Link } from 'react-router-dom'
import {
  LayoutDashboard,
  List,
  PlusCircle,
  Server,
  Cpu,
  Zap,
} from 'lucide-react'
import { useSocket } from '../hooks/useSocket'

const NAV_ITEMS = [
  { to: '/',        label: 'Dashboard',   icon: LayoutDashboard, end: true },
  { to: '/jobs',    label: 'Jobs',        icon: List },
  { to: '/jobs/new',label: 'Submit Job',  icon: PlusCircle },
  { to: '/workers', label: 'Workers',     icon: Server },
]

export default function Navbar() {
  const { connected } = useSocket()

  return (
    <aside className="sidebar">
      {/* Logo */}
      <Link to="/" className="sidebar-logo" style={{ textDecoration: 'none' }}>
        <div className="sidebar-logo-icon">
          <Cpu size={18} color="#fff" />
        </div>
        <div>
          <span className="sidebar-logo-text">
            ProcExec
            <span className="sidebar-logo-sub">Process Execution System</span>
          </span>
        </div>
      </Link>

      {/* Navigation */}
      <nav className="sidebar-nav">
        <span className="sidebar-section-label">Navigation</span>
        {NAV_ITEMS.map(({ to, label, icon: Icon, end }) => (
          <NavLink
            key={to}
            to={to}
            end={end}
            className={({ isActive }) => `nav-link${isActive ? ' active' : ''}`}
          >
            <Icon className="nav-icon" size={18} />
            {label}
          </NavLink>
        ))}
      </nav>

      {/* Footer — connection indicator */}
      <div className="sidebar-footer">
        <div className="connection-status">
          <span className={`status-dot ${connected ? 'connected' : 'disconnected'}`} />
          <Zap size={12} style={{ opacity: 0.6 }} />
          {connected ? 'Live — connected' : 'Disconnected'}
        </div>
      </div>
    </aside>
  )
}
