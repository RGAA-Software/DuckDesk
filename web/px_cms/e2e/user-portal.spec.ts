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
  const state = { loginCalls: 0, meCalls: 0, get authenticated() { return authenticated } }
  await page.route('**/api/v1/**', async (route) => {
    const request = route.request()
    const path = new URL(request.url()).pathname
    if (path === '/api/v1/user/registration-policy') {
      return json(route, ok({ mode: 'closed' }))
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
    if (path === '/api/v1/user/me') {
      state.meCalls += 1
      return authenticated
        ? json(route, ok(profile))
        : json(route, { code: 'AUTH_REQUIRED', message: 'authentication required', data: null, request_id: 'test-request' }, 401)
    }
    if (path === '/api/v1/user/devices') return json(route, ok([]))
    if (path === '/api/v1/user/apps') return json(route, ok([]))
    if (path === '/api/v1/user/instances') return json(route, ok([]))
    if (path === '/api/v1/user/resources/summary') {
      return json(route, ok({ device_count: 0, application_count: 0, active_instance_count: 0 }))
    }
    return json(route, { code: 'RESOURCE_NOT_FOUND', message: 'not found', data: null, request_id: 'test-request' }, 404)
  })
  return state
}

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
