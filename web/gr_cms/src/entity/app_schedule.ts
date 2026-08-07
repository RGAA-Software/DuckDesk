export interface Application {
  app_id: string
  name: string
  game_exe_rel: string
  default_game_args: string
  encoder_fps: number
  encoder_bitrate: number
  encoder_format: string
  webrtc_enabled: boolean
  websocket_enabled: boolean
}

export interface AppPlacement {
  placement_id: string
  app_id: string
  device_id: string
  install_root: string
}

export type InstanceState = 'starting' | 'running' | 'failed' | 'stopping' | 'stopped'

export interface AppInstance {
  instance_id: string
  request_id: string
  app_id: string
  device_id: string
  placement_id: string
  state: InstanceState
  listen_port: number
  pid: number
  error: string
  web_client_hint: string
}

export interface CreateApplicationReq {
  name: string
  game_exe_rel: string
  default_game_args?: string
  encoder_fps?: number
  encoder_bitrate?: number
  encoder_format?: string
}

export interface CreatePlacementReq {
  app_id: string
  device_id: string
  install_root: string
}

export interface StartInstanceReq {
  app_id: string
  device_id: string
  listen_port?: number
}
