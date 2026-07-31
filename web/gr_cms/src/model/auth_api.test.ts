import { describe, it, expect, vi, beforeEach } from 'vitest'
import axios from 'axios'
import axiosHttp from '@/http.ts'
import { ElNotification } from 'element-plus'
import { queryAuthorization, queryAuthStatus, pullAuthorization } from './auth_api.ts'

vi.mock('@/http.ts', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
  },
}))

vi.mock('element-plus', () => ({
  ElNotification: vi.fn(),
}))

describe('queryAuthorization', () => {
  beforeEach(() => {
    vi.mocked(axiosHttp.get).mockReset()
    localStorage.clear()
  })

  it('sends the stored appkey as a query parameter', async () => {
    localStorage.setItem('appkey', 'test-appkey-123')
    vi.mocked(axiosHttp.get).mockResolvedValue({
      status: 200,
      data: { code: 200, data: { appkey: 'test-appkey-123' } },
    } as any)

    await queryAuthorization()

    expect(axiosHttp.get).toHaveBeenCalledWith(
      '/api/v1/auth/control/get/authorization',
      {
        params: { appkey: 'test-appkey-123' },
      },
    )
  })
})

describe('queryAuthStatus', () => {
  beforeEach(() => {
    vi.mocked(axiosHttp.get).mockReset()
  })

  it('queries the status endpoint without an appkey', async () => {
    const status = { authorized: false, mode: '', valid: false, machine_code: '1234-5678' }
    vi.mocked(axiosHttp.get).mockResolvedValue({
      status: 200,
      data: { code: 200, data: status },
    } as any)

    const result = await queryAuthStatus()

    expect(axiosHttp.get).toHaveBeenCalledWith('/api/v1/auth/control/get/auth/status')
    expect(result).toEqual(status)
  })
})

describe('pullAuthorization', () => {
  beforeEach(() => {
    vi.mocked(axiosHttp.post).mockReset()
    vi.mocked(ElNotification).mockReset()
    localStorage.clear()
  })

  it('posts to the pull endpoint without an appkey and returns the status', async () => {
    const status = { authorized: true, mode: 'trial', valid: true }
    vi.mocked(axiosHttp.post).mockResolvedValue({
      status: 200,
      data: { code: 200, data: status },
    } as any)

    const result = await pullAuthorization()

    expect(axiosHttp.post).toHaveBeenCalledWith('/api/v1/auth/control/pull/authorization')
    expect(result).toEqual(status)
    expect(ElNotification).toHaveBeenCalledWith(
      expect.objectContaining({ message: '授权已刷新', type: 'success' }),
    )
  })

  it('notifies failure when the server returns a non-200 code', async () => {
    vi.mocked(axiosHttp.post).mockResolvedValue({
      status: 200,
      data: { code: 500, data: null },
    } as any)

    const result = await pullAuthorization()

    expect(result).toBeNull()
    expect(ElNotification).toHaveBeenCalledWith(
      expect.objectContaining({ message: '刷新授权失败:500', type: 'error' }),
    )
  })

  it('notifies a network error when the request fails', async () => {
    vi.mocked(axiosHttp.post).mockRejectedValue(new axios.AxiosError('Network Error'))

    const result = await pullAuthorization()

    expect(result).toBeNull()
    expect(ElNotification).toHaveBeenCalledWith(
      expect.objectContaining({ message: '刷新授权失败, 网络错误', type: 'error' }),
    )
  })
})
