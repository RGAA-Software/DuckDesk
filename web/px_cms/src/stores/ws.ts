// src/stores/ws.ts
import { defineStore } from 'pinia'
import { WsBaseMsg } from '@/entity/ws_base_msg.ts'

let ws: WebSocket | null = null
let reconnectTimer: ReturnType<typeof setTimeout> | null = null
let reconnectAttempts = 0
let activeUrl = ''
let shouldReconnect = false

export const useWsStore = defineStore('ws', {
  state: () => ({
    connected: false,
    message: null as WsBaseMsg | null,
  }),

  actions: {
    connect(url: string) {
      activeUrl = url
      shouldReconnect = true
      if (ws && (ws.readyState === WebSocket.CONNECTING || ws.readyState === WebSocket.OPEN)) return
      if (reconnectTimer) {
        clearTimeout(reconnectTimer)
        reconnectTimer = null
      }

      const socket = new WebSocket(url)
      ws = socket
      let heartbeatTimer: ReturnType<typeof setInterval> | null = null
      let heartbeatIndex: number = 0

      const startHeartbeat = () => {
        heartbeatTimer = setInterval(() => {
          heartbeatIndex++
          // heartbeat
          if (socket.readyState !== WebSocket.OPEN) return
          socket.send(
            JSON.stringify({
              msg_type: 'heartbeat',
              index: heartbeatIndex,
            }),
          )
        }, 2000)
      }

      socket.onopen = () => {
        if (ws !== socket) return
        reconnectAttempts = 0
        this.connected = true
        console.log('WebSocket connected')
        // ping
        socket.send(
          JSON.stringify({
            msg_type: 'ping',
          }),
        )

        startHeartbeat()
      }

      socket.onmessage = (e) => {
        if (ws !== socket) return
        this.message = JSON.parse(e.data)
      }

      socket.onclose = () => {
        const wasCurrent = ws === socket
        if (wasCurrent) ws = null
        this.connected = false
        console.log('WebSocket closed')
        if (heartbeatTimer) {
          clearInterval(heartbeatTimer)
          heartbeatTimer = null
        }
        if (wasCurrent && shouldReconnect && activeUrl && !reconnectTimer) {
          const delay = Math.min(1000 * 2 ** reconnectAttempts, 10000)
          reconnectAttempts++
          reconnectTimer = setTimeout(() => {
            reconnectTimer = null
            this.connect(activeUrl)
          }, delay)
        }
      }

      socket.onerror = (e) => {
        console.error('WebSocket error', e)
      }
    },

    send(data: unknown) {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(data))
        //ws.send(data)
      }
    },

    close() {
      shouldReconnect = false
      activeUrl = ''
      reconnectAttempts = 0
      if (reconnectTimer) {
        clearTimeout(reconnectTimer)
        reconnectTimer = null
      }
      const socket = ws
      ws = null
      socket?.close()
      this.connected = false
    },
  },
})
