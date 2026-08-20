import axiosHttp, { setAdminCsrfToken } from '@/http.ts'

export interface AdminProfile {
  auth_id: string
  username: string
}

export async function loginAdmin(username: string, password: string): Promise<AdminProfile | null> {
  const response = await axiosHttp.post('/api/v1/session/admin/login', { username, password })
  if (response.status !== 200 || response.data?.code !== 200) return null
  setAdminCsrfToken(response.data.data.csrf_token || '')
  return response.data.data.profile as AdminProfile
}

export async function queryAdminSession(): Promise<AdminProfile | null> {
  try {
    const response = await axiosHttp.get('/api/v1/session/admin/me')
    if (response.status !== 200 || response.data?.code !== 200) return null
    return response.data.data as AdminProfile
  } catch {
    return null
  }
}

export async function logoutAdmin(): Promise<void> {
  try {
    await axiosHttp.post('/api/v1/session/admin/logout')
  } finally {
    setAdminCsrfToken('')
  }
}
