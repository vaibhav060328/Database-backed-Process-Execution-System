import { useEffect, useRef, useState } from 'react'
import { io } from 'socket.io-client'

const SOCKET_URL = 'http://localhost:3000'

/**
 * useSocket — connects to the Socket.io server and returns
 * the socket instance and connection state.
 *
 * @returns {{ socket: Socket|null, connected: boolean }}
 */
export function useSocket() {
  const socketRef           = useRef(null)
  const [socket,    setSocket]    = useState(null)
  const [connected, setConnected] = useState(false)

  useEffect(() => {
    const s = io(SOCKET_URL, {
      transports: ['websocket'],
      reconnectionAttempts: 10,
      reconnectionDelay: 2000,
    })

    socketRef.current = s
    setSocket(s)

    s.on('connect',    () => setConnected(true))
    s.on('disconnect', () => setConnected(false))

    return () => {
      s.disconnect()
      socketRef.current = null
      setSocket(null)
    }
  }, [])

  return { socket, connected }
}

/**
 * useJobStatus — subscribes to real-time status updates for all jobs.
 * Calls onUpdate(payload) whenever `job:status` fires.
 *
 * payload: { id, status, workerId? }
 */
export function useJobStatus(onUpdate) {
  const { socket, connected } = useSocket()

  useEffect(() => {
    if (!socket) return
    socket.on('job:status', onUpdate)
    return () => socket.off('job:status', onUpdate)
  }, [socket, onUpdate])

  return connected
}

/**
 * useJobLog — subscribes to log lines for a specific job.
 * Calls onLine(line) for each `job:log` event matching the jobId.
 *
 * payload: { id, line, level? }  — level: 'info' | 'warn' | 'error' | 'success'
 */
export function useJobLog(jobId, onLine) {
  const { socket, connected } = useSocket()

  useEffect(() => {
    if (!socket || !jobId) return
    const handler = (payload) => {
      if (String(payload.id) === String(jobId)) onLine(payload)
    }
    socket.on('job:log', handler)
    return () => socket.off('job:log', handler)
  }, [socket, jobId, onLine])

  return connected
}

/**
 * useWorkerUpdates — subscribes to real-time worker heartbeats.
 * Calls onUpdate(worker) whenever `worker:update` fires.
 *
 * payload: { id, status, cpu, memory, currentJobId?, lastHeartbeat }
 */
export function useWorkerUpdates(onUpdate) {
  const { socket, connected } = useSocket()

  useEffect(() => {
    if (!socket) return
    socket.on('worker:update', onUpdate)
    return () => socket.off('worker:update', onUpdate)
  }, [socket, onUpdate])

  return connected
}
