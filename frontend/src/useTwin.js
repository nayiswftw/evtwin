import { useCallback, useEffect, useRef, useState } from 'react'

// Connects to the FastAPI WebSocket, keeps the latest state and a short history.
export function useTwin() {
  const [state, setState] = useState(null)
  const [connected, setConnected] = useState(false)
  const [history, setHistory] = useState([])
  const wsRef = useRef(null)
  const lastHist = useRef(0)

  useEffect(() => {
    let stop = false
    let timer
    const connect = () => {
      const proto = location.protocol === 'https:' ? 'wss' : 'ws'
      const ws = new WebSocket(`${proto}://${location.host}/ws?role=web`)
      wsRef.current = ws
      ws.onopen = () => setConnected(true)
      ws.onclose = () => {
        setConnected(false)
        if (!stop) timer = setTimeout(connect, 1500)
      }
      ws.onmessage = (e) => {
        const m = JSON.parse(e.data)
        if (m.type !== 'state') return
        setState(m)
        const now = performance.now()
        if (now - lastHist.current > 500) {
          lastHist.current = now
          setHistory((h) => [...h.slice(-119), { bal: m.total_kw, unm: m.unmanaged_kw, base: m.base_kw, cap: m.capacity, clock: m.clock }])
        }
      }
    }
    connect()
    return () => {
      stop = true
      clearTimeout(timer)
      wsRef.current?.close()
    }
  }, [])

  const send = useCallback((cmd) => {
    const ws = wsRef.current
    if (ws && ws.readyState === 1) ws.send(JSON.stringify(cmd))
  }, [])

  return { state, connected, history, send }
}
