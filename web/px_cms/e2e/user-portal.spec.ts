import { expect, test, type Page, type Route } from '@playwright/test'

const ok = (data: unknown) => ({ code: 200, message: 'ok', data })
const profile = {
  uid: 'u1',
  username: 'user-one',
  avatar_path: '',
  created_timestamp: 1,
  must_change_password: false,
  groups: [{ gid: 'g1', name: 'Group 1' }],
}

async function json(route: Route, data: unknown, status = 200, headers: Record<string, string> = {}) {
  await route.fulfill({
    status,
    contentType: 'application/json; charset=utf-8',
    headers,
    body: JSON.stringify(data),
  })
}

async function installUserApi(page: Page, initiallyAuthenticated = true) {
  let authenticated = initiallyAuthenticated
  const state = {
    loginCalls: 0,
    meCalls: 0,
    csrfCalls: 0,
    registerCalls: 0,
    ticketCalls: 0,
    ticketBody: undefined as Record<string, unknown> | undefined,
    ticketCsrf: '',
    apps: [] as Array<Record<string, unknown>>,
    registerBody: undefined as Record<string, unknown> | undefined,
    get authenticated() { return authenticated },
  }
  await page.route('**/api/v1/**', async (route) => {
    const request = route.request()
    const path = new URL(request.url()).pathname
    if (path === '/api/v1/session/guest') {
      return json(route, ok({ csrf_token: 'csrf-guest', expires_at: 9_999_999 }))
    }
    if (path === '/api/v1/user/register') {
      state.registerCalls += 1
      state.registerBody = request.postDataJSON()
      return json(route, ok({ uid: 'u-new', username: state.registerBody?.username }))
    }
    if (path === '/api/v1/session/user/login') {
      state.loginCalls += 1
      authenticated = true
      return json(
        route,
        ok({ profile, csrf_token: 'csrf-user', expires_at: 9_999_999, absolute_expires_at: 9_999_999 }),
        200,
        { 'set-cookie': 'px_user_session=test-cookie; Path=/; HttpOnly; SameSite=Strict' },
      )
    }
    if (path === '/api/v1/session/user/logout') {
      authenticated = false
      return json(route, ok({ revoked: true }), 200, {
        'set-cookie': 'px_user_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0',
      })
    }
    if (path === '/api/v1/session/user/csrf') {
      state.csrfCalls += 1
      return authenticated
        ? json(route, ok({ csrf_token: 'csrf-recovered' }))
        : json(route, { code: 'AUTH_REQUIRED', message: 'authentication required', data: null }, 401)
    }
    if (path === '/api/v1/user/me') {
      state.meCalls += 1
      return authenticated
        ? json(route, ok(profile))
        : json(route, { code: 'AUTH_REQUIRED', message: 'authentication required', data: null, request_id: 'test-request' }, 401)
    }
    if (path === '/api/v1/user/devices') return json(route, ok([]))
    if (path === '/api/v1/user/devices/page') return json(route, ok({ items: [], page: 1, page_size: 10, total: 0 }))
    if (path === '/api/v1/user/apps') return json(route, ok(state.apps))
    if (path === '/api/v1/user/apps/page') return json(route, ok({ items: state.apps, page: 1, page_size: 9, total: state.apps.length }))
    if (/^\/api\/v1\/user\/instances\/[^/]+\/ticket$/.test(path)) {
      state.ticketCalls += 1
      state.ticketBody = request.postDataJSON()
      state.ticketCsrf = request.headers()['x-csrf-token'] || ''
      const origin = new URL(request.url()).origin
      return json(route, ok({
        launch_url: `${origin}/user/apps?opened=1#ticket=ticket-1&renew=renew-1&nonce=nonce-1`,
        renewal_token: 'renew-1',
        permissions: state.ticketBody?.requested_permissions,
      }))
    }
    if (path === '/api/v1/user/instances') return json(route, ok([]))
    if (path === '/api/v1/user/instances/page') return json(route, ok({ items: [], page: 1, page_size: 10, total: 0 }))
    if (path === '/api/v1/user/resources/summary') {
      return json(route, ok({ device_count: 0, application_count: 0, active_instance_count: 0 }))
    }
    return json(route, { code: 'RESOURCE_NOT_FOUND', message: 'not found', data: null, request_id: 'test-request' }, 404)
  })
  return state
}

test('registration is directly available without an invitation code', async ({ page }) => {
  const api = await installUserApi(page, false)

  await page.goto('/user/login')
  await page.getByRole('button', { name: '注册账号' }).click()
  const dialog = page.getByRole('dialog', { name: '注册 Pixels 用户' })
  await expect(dialog.getByText('邀请码')).toHaveCount(0)
  await dialog.locator('input[autocomplete="username"]').fill('new-user')
  await dialog.locator('input[autocomplete="new-password"]').fill('safe-password-123')
  await dialog.locator('.ant-btn-primary').click()

  await expect.poll(() => api.registerCalls).toBe(1)
  expect(api.registerBody).toEqual({ username: 'new-user', password: 'safe-password-123' })
})

test('user portal never creates the admin websocket', async ({ page }) => {
  const webSockets: string[] = []
  page.on('websocket', (socket) => webSockets.push(socket.url()))
  await installUserApi(page)

  await page.goto('/user/devices')
  await expect(page.locator('main').getByText('我的远程桌面', { exact: true })).toBeVisible()
  await expect(page.getByText('管理员尚未向你或你的用户组授权设备')).toBeVisible()
  const applicationSockets = webSockets.filter((value) => new URL(value).pathname === '/cms')
  expect(applicationSockets).toEqual([])
})

test('login returns to the requested user page and logout invalidates it', async ({ page, context }) => {
  const api = await installUserApi(page, false)

  await page.goto('/user/apps')
  await expect(page).toHaveURL(/\/user\/login\?redirect=(?:%2F|\/)user(?:%2F|\/)apps$/)
  await page.locator('input[autocomplete="username"]').fill('user-one')
  const password = page.locator('input[autocomplete="current-password"]')
  await password.fill('correct-password')
  await page.getByRole('button', { name: /登\s*录/ }).click()
  await expect.poll(() => api.loginCalls).toBe(1)
  await expect.poll(() => new URL(page.url()).pathname).toBe('/user/apps')
  expect(api.authenticated).toBe(true)
  // A full navigation proves the authenticated return target is durable and
  // not merely a stale SPA URL while the login component remains mounted.
  await page.reload()
  await expect(page.getByText('云端应用', { exact: true }).first()).toBeVisible()

  const cookie = (await context.cookies()).find((item) => item.name === 'px_user_session')
  expect(cookie?.httpOnly).toBe(true)
  expect(await page.evaluate(() => document.cookie)).not.toContain('px_user_session')

  await page.getByRole('button', { name: '退出登录' }).click()
  await expect(page).toHaveURL(/\/user\/login$/)
  expect((await context.cookies()).some((item) => item.name === 'px_user_session')).toBe(false)

  await page.goto('/user/apps')
  await expect(page).toHaveURL(/\/user\/login\?redirect=(?:%2F|\/)user(?:%2F|\/)apps$/)
})

test('a fresh authenticated tab recovers csrf and opens an authorized application', async ({ page }) => {
  const api = await installUserApi(page)
  api.apps.push({
    app_id: 'app-1',
    name: 'Authorized App',
    access_mode: 'acl',
    cover_url: '',
    version: 1,
    running_instance: { instance_id: 'instance-1', state: 'running', reconnectable: true },
  })

  await page.goto('/user/apps')
  await expect(page.getByText('Authorized App')).toBeVisible()
  await expect(page.getByText('用户组专属应用')).toBeVisible()
  await expect.poll(() => api.csrfCalls).toBe(1)
  await page.getByRole('button', { name: /进\s*入/ }).click()

  await expect.poll(() => api.ticketCalls).toBe(1)
  expect(api.ticketCsrf).toBe('csrf-recovered')
  expect(api.ticketBody?.requested_permissions).toEqual([
    'view',
    'input',
    'clipboard',
    'file',
    'audio',
  ])
  await expect(page).toHaveURL(/opened=1/)
  const fragment = new URLSearchParams(new URL(page.url()).hash.slice(1))
  expect(fragment.get('renew')).toBe('renew-1')
  expect(fragment.get('renew_url')).toContain('/api/v1/connection-tickets/renew')
  expect(fragment.get('perms')).toBe('view,input,clipboard,file,audio')
})

test('view-only application entry requests a server-enforced view grant', async ({ page }) => {
  const api = await installUserApi(page)
  api.apps.push({
    app_id: 'app-1',
    name: 'Authorized App',
    access_mode: 'acl',
    cover_url: '',
    version: 1,
    running_instance: { instance_id: 'instance-1', state: 'running', reconnectable: true },
  })

  await page.goto('/user/apps')
  await page.getByRole('button', { name: '仅观看' }).click()

  await expect.poll(() => api.ticketCalls).toBe(1)
  expect(api.ticketBody?.requested_permissions).toEqual(['view'])
  await expect(page).toHaveURL(/opened=1/)
  const fragment = new URLSearchParams(new URL(page.url()).hash.slice(1))
  expect(fragment.get('perms')).toBe('view')
})

test('public catalog is anonymous and recreates a stale guest session for owned instances', async ({ page }) => {
  let guestCalls = 0
  let instanceCalls = 0
  let publicAuthorization = ''
  await page.route('**/api/v1/**', async (route) => {
    const request = route.request()
    const path = new URL(request.url()).pathname
    if (path === '/api/v1/public/apps') {
      publicAuthorization = request.headers().authorization || ''
      return json(route, ok([{ app_id: 'public-1', name: 'Public App', access_mode: 'public', cover_url: '', version: 1 }]))
    }
    if (path === '/api/v1/session/guest') {
      guestCalls += 1
      return json(route, ok({ csrf_token: `guest-csrf-${guestCalls}` }))
    }
    if (path === '/api/v1/public/instances') {
      instanceCalls += 1
      if (instanceCalls === 1) {
        return json(route, { code: 'AUTH_REQUIRED', message: 'stale guest', data: null }, 401)
      }
      return json(route, ok([]))
    }
    return json(route, { code: 'RESOURCE_NOT_FOUND', message: 'not found', data: null }, 404)
  })

  await page.goto('/user/public-apps')
  await expect(page.getByText('Public App')).toBeVisible()
  await expect.poll(() => guestCalls).toBe(2)
  await expect.poll(() => instanceCalls).toBe(2)
  expect(publicAuthorization).toBe('')
})
