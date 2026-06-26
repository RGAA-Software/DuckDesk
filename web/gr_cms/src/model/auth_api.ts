import axiosHttp from '@/http.ts'
import { ElNotification } from 'element-plus'
import axios from "axios";

export async function updateAuthorization(authStr: string) {
  try {
    const resp = await axiosHttp.post(
      '/api/v1/auth/control/update/authorization?appkey=' + localStorage.getItem('appkey'),
      {
        data: authStr,
      },
    )
    if (resp.status !== 200) {
      console.error('change password failed', resp)
      return false
    }

    const data = resp.data
    if (data.code !== 200) {
      console.error('update authorization failed, data:', data)
      ElNotification({
        message: '更新授权失败:' + data.code,
        type: 'error',
      })
    } else {
      console.log('update authorization success')
      ElNotification({
        message: '更新授权成功',
        type: 'success',
      })
      return data.data
    }
  } catch (e) {
    console.error(e)
    if (axios.isAxiosError(e)) {
      const status = e.response?.status
      if (status === 623) {
        ElNotification({
          message: '机器码不一致，请重新获取授权',
          type: 'error',
        })
      } else {
        ElNotification({
          message: '更新授权失败, 网络错误',
          type: 'error',
        })
      }
    }
  }
}

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
