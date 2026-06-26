import type { AxiosAdapter, AxiosResponse, InternalAxiosRequestConfig } from 'axios'
import { beforeEach, describe, expect, it, vi } from 'vitest'

const messageError = vi.fn()
const locationHref = vi.fn()
const locationPathname = vi.fn()

Object.defineProperty(window, 'location', {
  value: {
    get pathname() { return locationPathname() },
    set href(value: string) { locationHref(value) },
  },
  writable: true,
})

vi.mock('element-plus', () => ({
  ElMessage: {
    error: messageError,
  },
}))

const okResponse = (config: InternalAxiosRequestConfig): AxiosResponse => ({
  data: { code: 200, data: {} },
  status: 200,
  statusText: 'OK',
  headers: {},
  config,
})

describe('http client interceptors', () => {
  beforeEach(() => {
    sessionStorage.clear()
    locationHref.mockReset()
    locationPathname.mockReset()
    messageError.mockReset()
    locationPathname.mockReturnValue('/')
  })

  it('adds Authorization header when login token exists', async () => {
    const { default: http } = await import('./http')
    sessionStorage.setItem('login_token', 'token-a')

    const adapter: AxiosAdapter = async (config) => okResponse(config)
    const response = await http.get('/me', { adapter })

    expect(response.config.headers.Authorization).toBe('token-a')
  })

  it('clears token and redirects to login on HTTP 401', async () => {
    const { default: http } = await import('./http')
    sessionStorage.setItem('login_token', 'token-a')
    sessionStorage.setItem('login_role', 'admin')
    locationPathname.mockReturnValue('/main/auth-list')

    const adapter: AxiosAdapter = async (config) => Promise.reject({
      config,
      response: {
        status: 401,
        data: { code: 810 },
      },
    })

    await expect(http.get('/me', { adapter })).rejects.toBeTruthy()

    expect(sessionStorage.getItem('login_token')).toBeNull()
    expect(sessionStorage.getItem('login_role')).toBeNull()
    expect(messageError).toHaveBeenCalledWith('登录已失效，请重新登录')
    expect(locationHref).toHaveBeenCalledWith('/')
  })

  it('clears token on legacy login-expired business code in success response', async () => {
    const { default: http } = await import('./http')
    sessionStorage.setItem('login_token', 'token-a')
    sessionStorage.setItem('login_role', 'admin')
    locationPathname.mockReturnValue('/main/auth-list')

    const adapter: AxiosAdapter = async (config) => ({
      ...okResponse(config),
      data: { code: 811 },
    })

    await http.get('/me', { adapter })
    await Promise.resolve()

    expect(sessionStorage.getItem('login_token')).toBeNull()
    expect(sessionStorage.getItem('login_role')).toBeNull()
    expect(locationHref).toHaveBeenCalledWith('/')
  })

  it('shows permission error without clearing token on HTTP 403', async () => {
    const { default: http } = await import('./http')
    sessionStorage.setItem('login_token', 'token-a')
    sessionStorage.setItem('login_role', 'admin')
    locationPathname.mockReturnValue('/main/auth-list')

    const adapter: AxiosAdapter = async (config) => Promise.reject({
      config,
      response: {
        status: 403,
        data: { code: 803 },
      },
    })

    await expect(http.get('/query/authors', { adapter })).rejects.toBeTruthy()

    expect(sessionStorage.getItem('login_token')).toBe('token-a')
    expect(sessionStorage.getItem('login_role')).toBe('admin')
    expect(messageError).toHaveBeenCalledWith('没有权限执行该操作')
    expect(locationHref).not.toHaveBeenCalled()
  })

  it('does not clear token or redirect on HTTP 500', async () => {
    const { default: http } = await import('./http')
    sessionStorage.setItem('login_token', 'token-a')
    locationPathname.mockReturnValue('/main/auth-list')

    const adapter: AxiosAdapter = async (config) => Promise.reject({
      config,
      response: {
        status: 500,
        data: { code: 805 },
      },
    })

    await expect(http.get('/query/authorizations', { adapter })).rejects.toBeTruthy()

    expect(sessionStorage.getItem('login_token')).toBe('token-a')
    expect(messageError).not.toHaveBeenCalled()
    expect(locationHref).not.toHaveBeenCalled()
  })

  it('does not clear token or redirect when request has no response', async () => {
    const { default: http } = await import('./http')
    sessionStorage.setItem('login_token', 'token-a')
    locationPathname.mockReturnValue('/main/auth-list')

    const adapter: AxiosAdapter = async (config) => Promise.reject({
      config,
      request: {},
    })

    await expect(http.get('/query/authorizations', { adapter })).rejects.toBeTruthy()

    expect(sessionStorage.getItem('login_token')).toBe('token-a')
    expect(messageError).not.toHaveBeenCalled()
    expect(locationHref).not.toHaveBeenCalled()
  })
})
