import axiosHttp from '@/http.ts'

export type RtcCredentialMode = 'none' | 'static' | 'console_ephemeral'

export interface ManagedTurnServerConfig {
  enabled: boolean
  listen_ip: string
  public_host: string
  port: number
  relay_min_port: number
  relay_max_port: number
  realm: string
  enable_udp: boolean
  enable_tcp: boolean
  credential_ttl_seconds: number
}

export interface AdditionalIceServerConfig {
  id: string
  name: string
  enabled: boolean
  urls: string[]
  credential_mode: RtcCredentialMode
  username: string
  credential: string
}

export interface RtcIceConfig {
  revision: number
  direct_probe_enabled: boolean
  managed_console_server: ManagedTurnServerConfig
  additional_servers: AdditionalIceServerConfig[]
}

export interface TurnSidecarStatus {
  enabled: boolean
  running: boolean
  pid?: number
  listen_ip: string
  public_host: string
  port: number
  relay_min_port: number
  relay_max_port: number
  revision: number
  last_error: string
}

function unwrap<T>(response: { data: { code: number; message?: string; data: T } }): T {
  if (response.data.code !== 200) throw new Error(response.data.message || '请求失败')
  return response.data.data
}

export async function getRtcIceConfig() {
  return unwrap<RtcIceConfig>(await axiosHttp.get('/api/v1/admin/rtc/ice-config'))
}

export async function testRtcIceConfig(config: RtcIceConfig) {
  return unwrap<boolean>(await axiosHttp.post('/api/v1/admin/rtc/ice-config/test', config))
}

export async function saveRtcIceConfig(config: RtcIceConfig) {
  return unwrap<RtcIceConfig>(await axiosHttp.put('/api/v1/admin/rtc/ice-config', config))
}

export async function getTurnStatus() {
  return unwrap<TurnSidecarStatus>(await axiosHttp.get('/api/v1/admin/rtc/turn-status'))
}
