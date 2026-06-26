import { describe, it, expect, vi, beforeEach } from 'vitest'
import axiosHttp from '@/http.ts'
import { queryAuthorization } from './auth_api.ts'

vi.mock('@/http.ts', () => ({
  default: {
    get: vi.fn(),
  },
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
