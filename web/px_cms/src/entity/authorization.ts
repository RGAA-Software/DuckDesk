export interface Authorization {
  auth_id: string
  auth_name: string
  machine_code: string
  username: string
  role: number
  days: number
  max_streams: number
  end_timestamp_ms: number
  used_time_ms: number
  created_timestamp_ms: number
  mode: string
}
