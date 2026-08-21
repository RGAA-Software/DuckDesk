import { beforeEach, describe, expect, it, vi } from 'vitest'
import axiosHttp from '@/http'
import { listAllAdminUsers, type UserAdminView } from './identity_api'

vi.mock('@/http', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
    patch: vi.fn(),
    put: vi.fn(),
    delete: vi.fn(),
  },
}))

function user(index: number): UserAdminView {
  return {
    uid: `u-${index}`,
    username: `user-${index}`,
    avatar_url: '',
    assigned: true,
    disabled: false,
    auth_version: 1,
    must_change_password: false,
    groups: [],
    created_at: index,
    updated_at: index,
    version: 1,
  }
}

describe('listAllAdminUsers', () => {
  beforeEach(() => vi.mocked(axiosHttp.get).mockReset())

  it('loads every page so group membership is not truncated at 100 users', async () => {
    const allUsers = Array.from({ length: 205 }, (_, index) => user(index + 1))
    vi.mocked(axiosHttp.get).mockImplementation(async (_path, config) => {
      const page = Number(config?.params?.page || 1)
      const pageSize = Number(config?.params?.page_size || 100)
      const start = (page - 1) * pageSize
      return {
        data: {
          data: {
            items: allUsers.slice(start, start + pageSize),
            total: allUsers.length,
          },
        },
      } as never
    })

    const result = await listAllAdminUsers()

    expect(result).toHaveLength(205)
    expect(result.at(-1)?.uid).toBe('u-205')
    expect(axiosHttp.get).toHaveBeenCalledTimes(3)
  })
})
