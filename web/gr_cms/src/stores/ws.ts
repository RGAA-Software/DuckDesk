// src/stores/ws.ts
import { defineStore } from 'pinia'
import { WsBaseMsg } from '@/entity/ws_base_msg.ts'

let ws: WebSocket | null = null

export const useWsStore = defineStore('ws', {
  state: () => ({
    connected: false,
    message: null as WsBaseMsg | null,
  }),

  actions: {
    connect(url: string) {
      if (ws) return

      ws = new WebSocket(url)
      let heartbeatTimer: number = 0
      let heartbeatIndex: number = 0

      const startHeartbeat = () => {
        heartbeatTimer = setInterval(() => {
          heartbeatIndex++
          // heartbeat
          ws?.send(
            JSON.stringify({
              msg_type: 'heartbeat',
              index: heartbeatIndex,
            }),
          )
        }, 2000)
      }

      ws.onopen = () => {
        this.connected = true
        console.log('WebSocket connected')
        // ping
        ws?.send(
          JSON.stringify({
            msg_type: 'ping',
          }),
        )

        startHeartbeat()
      }

      ws.onmessage = (e) => {
        this.message = JSON.parse(e.data)
      }

      ws.onclose = () => {
        this.connected = false
        ws = null
        console.log('WebSocket closed')
        clearInterval(heartbeatTimer)
      }

      ws.onerror = (e) => {
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
      ws?.close()
      ws = null
    },
  },
})
