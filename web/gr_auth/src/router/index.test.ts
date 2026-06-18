import { afterEach, beforeEach, describe, expect, it } from 'vitest'

import router from './index'

describe('router auth guard', () => {
  beforeEach(() => {
    sessionStorage.clear()
  })

  afterEach(async () => {
    sessionStorage.clear()
    await router.replace('/')
  })

  it('redirects main routes to login when token is missing', async () => {
    await router.push('/main/auth-list')

    expect(router.currentRoute.value.path).toBe('/')
  })

  it('allows main routes when token exists', async () => {
    sessionStorage.setItem('login_token', 'valid-token')

    await router.push('/main/auth-list')

    expect(router.currentRoute.value.path).toBe('/main/auth-list')
  })
})
