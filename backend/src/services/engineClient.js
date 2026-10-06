/**
 * engineClient.js
 * Optional client bridge for direct IPC/RPC communication between
 * the Node.js backend and the C++ Execution Engine (if needed in addition to DB queue).
 */

class EngineClient {
  constructor() {
    this.connected = false
  }

  notifyJobQueued(jobId) {
    // Engine polls the DB directly, but this can send a fast wake-up signal (via socket / signal)
    console.log(`[EngineClient] Signal: Job #${jobId} queued.`)
  }

  notifyJobCancelled(jobId) {
    console.log(`[EngineClient] Signal: Job #${jobId} cancelled.`)
  }
}

module.exports = new EngineClient()
