import { BrowserRouter, Routes, Route } from 'react-router-dom'
import Navbar from './components/Navbar'
import Dashboard from './pages/Dashboard'
import JobList from './pages/JobList'
import JobSubmit from './pages/JobSubmit'
import JobDetail from './pages/JobDetail'
import Workers from './pages/Workers'
import './index.css'

function App() {
  return (
    <BrowserRouter>
      <div className="app-shell">
        <Navbar />
        <main className="app-main">
          <div className="page-container">
            <Routes>
              <Route path="/"           element={<Dashboard />} />
              <Route path="/jobs"       element={<JobList />} />
              <Route path="/jobs/new"   element={<JobSubmit />} />
              <Route path="/jobs/:id"   element={<JobDetail />} />
              <Route path="/workers"    element={<Workers />} />
            </Routes>
          </div>
        </main>
      </div>
    </BrowserRouter>
  )
}

export default App
