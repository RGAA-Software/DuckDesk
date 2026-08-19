import axiosHttp from '@/http.ts'

export interface LiveStatus {
  online: boolean
  stream_id: string
  app_id: string
  video_codec: string
  audio_codec: string
  width: number
  height: number
  fps: number
  reader_count: number
  browser_playable: boolean
  message: string
  play_url?: string
}

export async function queryLiveStatus(deviceId: string, appId: string): Promise<LiveStatus | null> {
  const response = await axiosHttp.get('/api/v1/live/control/status', {
    params: {
      device_id: deviceId.trim(),
      app_id: appId.trim(),
      appkey: localStorage.getItem('appkey'),
    },
  })
  if (response.status !== 200 || response.data?.code !== 200) {
    return null
  }
  return response.data.data as LiveStatus
}
