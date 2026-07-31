import axiosHttp from '@/http.ts'
import { ElNotification } from 'element-plus'
import axios from "axios";

// 授权状态（安全视图，后端 AuthStatus）：不含 appkey/app_secret，
// 因此无需 appkey 即可查询（登录页在未授权前展示用）。
// username/password 仅在服务器本机浏览器访问时由后端附带（用于自动填充登录表单）。
export interface AuthStatus {
  authorized: boolean
  mode: string // 'trial' | 'licensed' | ''（未授权）
  days: number
  max_streams: number
  end_timestamp_ms: number
  used_time_ms: number
  valid: boolean
  machine_code: string
  username?: string
  password?: string
}

// 手动触发一次向授权服务器的网络上报/拉取。该接口在 appkey filter 白名单内，
// 未授权前也可调用；返回安全状态（不含凭据）。
export async function pullAuthorization(): Promise<AuthStatus | null> {
  try {
    const resp = await axiosHttp.post('/api/v1/auth/control/pull/authorization')
    if (resp.status !== 200) {
      console.error('pull authorization failed', resp)
      ElNotification({
        message: '刷新授权失败:' + resp.status,
        type: 'error',
      })
      return null
    }

    const data = resp.data
    if (data.code !== 200) {
      console.error('pull authorization failed, data:', data)
      ElNotification({
        message: '刷新授权失败:' + data.code,
        type: 'error',
      })
      return null
    }

    console.log('pull authorization success')
    ElNotification({
      message: '授权已刷新',
      type: 'success',
    })
    return data.data
  } catch (e) {
    console.error(e)
    if (axios.isAxiosError(e)) {
      ElNotification({
        message: '刷新授权失败, 网络错误',
        type: 'error',
      })
    } else {
      ElNotification({
        message: '刷新授权失败',
        type: 'error',
      })
    }
    return null
  }
}

// 查询授权状态（安全视图，无需 appkey，未授权前可用）。
export async function queryAuthStatus(): Promise<AuthStatus | null> {
  try {
    const resp = await axiosHttp.get('/api/v1/auth/control/get/auth/status')
    if (resp.status !== 200) {
      console.error('query auth status failed', resp)
      return null
    }

    const data = resp.data
    if (data.code !== 200) {
      console.error('query auth status failed, data:', data)
      return null
    }
    return data.data
  } catch (e) {
    console.error(e)
    return null
  }
}

// 查询完整授权信息（含 appkey 等，受 appkey filter 保护，需登录后使用）。
export async function queryAuthorization() {
  const resp = await axiosHttp.get('/api/v1/auth/control/get/authorization', {
    params: {
      appkey: localStorage.getItem('appkey'),
    },
  })
  if (resp.status !== 200) {
    console.error('query users failed', resp)
    return null
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('query users failed, data:', data)
    return null
  }
  console.log('authorization: ', data.data)
  return data.data
}
