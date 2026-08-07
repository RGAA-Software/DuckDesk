// service 连接信息
export interface ServiceConn {
  device_id: string
  appkey: string
  version: string
  hello_timestamp: number
  last_update_timestamp: number
  hb_index: number
  render_alive: boolean
  auth_info_json: string
  /** Service HeartBeat 上报的本机实例摘要 JSON */
  instances_json?: string
}

// auth_info_json 解析后的授权信息
export interface ServiceAuthInfo {
  auth_name?: string
  role?: string
  days?: number
  max_streams?: number
  end_timestamp_ms?: number
}
