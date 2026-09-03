/** A current or historical logical remote-control session reported by Render. */
export interface RemoteSession {
  logical_session_id: string
  device_id: string
  stream_id: string
  subject_id: string
  role: 'controller' | 'observer' | string
  transports: string[]
  active: boolean
  /** Logical session displaced when this controller took over; empty otherwise. */
  takeover_previous_session_id: string
  opened_timestamp: number
  updated_timestamp: number
  closed_timestamp: number
}

/** An immutable lifecycle audit event; Console retains these records permanently. */
export interface RemoteSessionEvent {
  event_id: string
  device_id: string
  logical_session_id: string
  related_session_id: string
  event_type: string
  previous_role: string
  role: string
  previous_transports: string[]
  transports: string[]
  timestamp: number
}
