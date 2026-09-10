import { MessageType } from './protocol_enums'

/** Protocol liveness is distinct from diagnostic ping; reuse the authenticated media channel. */
export function startControlHeartbeat(send: (fields: Record<string, unknown>) => boolean): () => void {
  let active = true
  let index = 0
  const tick = () => {
    if (active) send({ type: MessageType.HeartBeat, heartbeat: { index: index++, timestamp: Date.now() } })
  }
  tick()
  const timer = setInterval(tick, 2000)
  return () => {
    active = false
    clearInterval(timer)
  }
}
