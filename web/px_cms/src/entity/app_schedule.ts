export interface Application {
  app_id: string
  name: string
  game_path?: string
  game_exe_rel: string
  default_game_args: string
  encoder_fps: number
  encoder_bitrate: number
  encoder_format: string
  webrtc_enabled: boolean
  websocket_enabled: boolean
  listen_port?: number
}

export interface AppPlacement {
  placement_id: string
  app_id: string
  device_id: string
  install_root: string
}

/** 节点:应用在某台机器上的一个可启动单元(机器+端口+安装目录)。 */
export interface AppNode {
  node_id: string
  app_id: string
  name: string
  device_id: string
  install_root: string
  listen_port: number
  last_run_at: number
  seq_no: number
}

export type InstanceState = 'starting' | 'running' | 'failed' | 'stopping' | 'stopped'

export interface AppInstance {
  instance_id: string
  request_id: string
  app_id: string
  device_id: string
  placement_id: string
  node_id?: string
  state: InstanceState
  listen_port: number
  pid: number
  error: string
  web_client_hint: string
  owner_type: 'guest' | 'user' | 'admin' | ''
  owner_id: string
  owner_session_id: string
  created_at_ms: number
}

/** Flattened app row from CMS /app/rows, nodes embedded. */
export interface AppRow {
  app_id: string
  name: string
  game_path: string
  default_game_args: string
  encoder_fps: number
  encoder_bitrate: number
  encoder_format: string
  access_mode: 'public' | 'acl'
  version: number
  nodes: AppNode[]
}

export interface SaveAppReq {
  app_id?: string
  name: string
  game_path: string
  default_game_args?: string
  encoder_fps?: number
  encoder_bitrate?: number
  encoder_format?: string
}

export interface SaveNodeReq {
  node_id?: string
  app_id: string
  name?: string
  device_id: string
  install_root?: string
  listen_port?: number
}

export interface StartInstanceReq {
  app_id: string
}
