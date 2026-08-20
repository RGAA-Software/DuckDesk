// panel 连接信息
export interface PanelConn {
  device_id: string
  device_name: string
  device_ip_addr: string
  user_id?: string
  hello_timestamp?: number
  last_update_timestamp?: number
  sys_info?: unknown
}
