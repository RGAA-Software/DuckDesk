// Actual Windows Auth executable + built Vue application + a disposable bootstrap database.
const assert = require('node:assert/strict')
const { spawn, execFileSync } = require('node:child_process')
const { createHash, randomUUID } = require('node:crypto')
const { createRequire } = require('node:module')
const { once } = require('node:events')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const repo = path.resolve(__dirname, '../..')
const { chromium } = createRequire(path.join(repo, 'web/px_desk/package.json'))('@playwright/test')
assert.equal(process.env.PIXELS_PG_ISOLATED_TEST, '1')
assert.equal(process.platform, 'win32')
const executable = process.argv[2], container = process.env.PIXELS_TEST_CONTAINER
assert.match(container || '', /^[a-f0-9]{12,64}$/)
const docker = (...args) => execFileSync('docker', args, { encoding: 'utf8', timeout: 45000, windowsHide: true })
assert.match(docker('inspect', container, '--format', '{{index .Config.Labels "com.docker.compose.project"}}').trim(), /^pixels-pg-\d{8}-\d{6}-[a-f0-9]{8}$/)
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'pixels-auth-browser-'))
const publicKey = Buffer.from('d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a', 'hex')
const key = Buffer.from('3053020101300506032b6570042204209d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60a123032100' + publicKey.toString('hex'), 'hex')
const password = 'synthetic-auth-password'
let child, browser, base, output = '', stopped = false
const timeLimit = (promise, milliseconds, message) => Promise.race([promise, new Promise((_, reject) => {
  const timer = setTimeout(() => reject(new Error(message)), milliseconds); timer.unref()
})])
async function stopServer() {
  if (child && child.exitCode === null && child.signalCode === null) {
    const exited = once(child, 'exit'); child.kill(); await timeLimit(exited, 10000, 'Auth shutdown timeout')
  }
  child = undefined
}
async function startServer() {
  const env = { ...process.env,
    PIXELS_DATABASE_URL: process.env.PIXELS_TEST_AUTH_RUNTIME_URL.replace(/\/pixels_auth$/, '/pixels_auth_bootstrap_windows'),
    PIXELS_AUTH_LOCAL_DEVELOPMENT: '1', PIXELS_AUTH_LISTEN: '127.0.0.1:0',
    PIXELS_AUTH_SIGNING_KEY: path.join(directory, 'signing.der'),
    PIXELS_AUTH_SIGNING_KEY_ID: createHash('sha256').update(publicKey).digest('hex'),
    PIXELS_AUTH_STATIC_DIRECTORY: path.join(repo, 'web/px_auth/dist') }
  delete env.PIXELS_AUTH_TLS_CERT; delete env.PIXELS_AUTH_TLS_KEY
  let startup = ''
  child = spawn(executable, [], { env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] })
  base = await timeLimit(new Promise((resolve, reject) => {
    child.once('error', reject); child.once('exit', () => reject(new Error('Auth exited before readiness')))
    child.stderr.on('data', bytes => { output += bytes.toString() })
    child.stdout.on('data', bytes => {
      output += bytes.toString(); startup += bytes.toString()
      const match = startup.match(/Auth listening (http:\/\/127\.0\.0\.1:\d+)/)
      if (match) resolve(match[1])
    })
  }), 20000, 'Auth startup timeout')
}
async function api(route, method = 'GET', body, token) {
  const response = await fetch(base + route, { method, headers: { 'content-type': 'application/json', ...(token ? { authorization: 'Bearer ' + token } : {}) },
    body: body === undefined ? undefined : JSON.stringify(body), signal: AbortSignal.timeout(22000) })
  const text = await response.text()
  return { status: response.status, body: text ? JSON.parse(text) : null }
}
async function run() {
  const identity = execFileSync('whoami', [], { encoding: 'utf8', windowsHide: true }).trim()
  execFileSync('icacls', [directory, '/inheritance:r', '/grant:r', identity + ':(OI)(CI)F', '*S-1-5-18:(OI)(CI)F'], { windowsHide: true })
  fs.writeFileSync(path.join(directory, 'signing.der'), key, { flag: 'wx' })
  await startServer()
  browser = await chromium.launch({ headless: true })
  const context = await browser.newContext()
  await context.addInitScript(() => localStorage.setItem('pixels_auth_language', 'en'))
  const external = []
  await context.route('**/*', route => {
    if (new URL(route.request().url()).origin === base) return route.continue()
    external.push(route.request().url()); return route.abort()
  })
  const page = await context.newPage(); page.setDefaultTimeout(15000)
  const pageErrors = []; page.on('pageerror', error => pageErrors.push(error.message))
  const login = async username => {
    await page.getByLabel('Username', { exact: true }).fill(username)
    await page.getByLabel('Password (12–256 bytes)', { exact: true }).fill(password)
    await page.getByRole('button', { name: 'Sign in', exact: true }).click()
    await page.getByRole('button', { name: 'Sign out', exact: true }).waitFor()
    return page.evaluate(() => sessionStorage.getItem('pixels_auth_session'))
  }
  await page.goto(base)
  const token = await login('bootstrap-admin')
  assert.match(token, /^[a-f0-9]{64}$/)
  await page.getByRole('button', { name: 'Customers', exact: true }).click()
  const customerName = 'Browser customer ' + randomUUID()
  await page.getByLabel('Name', { exact: true }).fill(customerName)
  await page.getByLabel('Remark', { exact: true }).fill('Synthetic private remark')
  const created = page.waitForResponse(r => r.url().endsWith('/api/auth/customers') && r.request().method() === 'POST')
  await page.getByRole('button', { name: 'Create', exact: true }).click()
  const customerResponse = await created; assert.equal(customerResponse.status(), 201)
  const customer = await customerResponse.json()
  await page.getByRole('cell', { name: customerName, exact: true }).waitFor()
  console.log('PASS auth-browser/login-create-customer')

  await page.getByRole('button', { name: 'Licenses', exact: true }).click()
  await page.getByRole('button', { name: 'Issue license', exact: true }).click()
  const form = page.locator('form')
  const deployment = randomUUID(), machine = 'c'.repeat(64)
  await form.getByLabel('Customer UUID', { exact: true }).fill(customer.id)
  await form.getByLabel('Deployment UUID', { exact: true }).fill(deployment)
  await form.getByLabel('Machine SHA-256', { exact: true }).fill(machine)
  // Lose an already-committed response: the UI must retain the same request ID for retry.
  let committed, requestId
  await page.route('**/api/auth/licenses/issue', async route => {
    requestId = route.request().postDataJSON().request_id
    const response = await route.fetch(); assert.equal(response.status(), 200)
    committed = await response.json(); await route.abort('failed')
  })
  await form.getByRole('button', { name: 'Save', exact: true }).click()
  await form.getByRole('alert').waitFor()
  assert.ok(committed)
  await page.unroute('**/api/auth/licenses/issue')
  const retry = page.waitForResponse(r => r.url().endsWith('/api/auth/licenses/issue'))
  await form.getByRole('button', { name: 'Save', exact: true }).click()
  const retried = await retry
  assert.equal(retried.request().postDataJSON().request_id, requestId)
  assert.deepEqual(await retried.json(), committed)
  const row = page.locator('[data-license="' + committed.license_id + '"]'); await row.waitFor()
  const verify = { wire: committed.wire, deployment_id: deployment, product: 'pixels_console', distribution: 'customer', machine_sha256: machine }
  assert.equal((await api('/api/auth/licenses/verify', 'POST', verify)).status, 200)
  console.log('PASS auth-browser/commit-response-loss-exact-retry')

  await row.getByRole('button', { name: 'Renew', exact: true }).click()
  assert.equal(await form.getByLabel('Deployment UUID', { exact: true }).isDisabled(), true)
  await form.getByLabel('Session limit', { exact: true }).fill('3')
  const renewed = page.waitForResponse(r => r.url().endsWith('/api/auth/licenses/issue'))
  await form.getByRole('button', { name: 'Save', exact: true }).click()
  const renewal = await (await renewed).json(); assert.equal(renewal.revision, 2)
  assert.equal((await api('/api/auth/licenses/verify', 'POST', verify)).status, 401)
  verify.wire = renewal.wire
  assert.equal((await api('/api/auth/licenses/verify', 'POST', verify)).status, 200)
  await row.getByRole('button', { name: 'Revoke', exact: true }).click()
  await page.locator('div.card').filter({ hasText: 'Revoke this license?' }).getByRole('button', { name: 'Revoke', exact: true }).click()
  await row.getByRole('cell', { name: 'Revoked', exact: true }).waitFor()
  assert.equal((await api('/api/auth/licenses/verify', 'POST', verify)).status, 401)
  console.log('PASS auth-browser/renew-and-revoke')

  await page.getByLabel('Theme', { exact: true }).selectOption('light')
  assert.equal(await page.locator('html').getAttribute('data-theme'), 'light')
  await page.getByLabel('Language', { exact: true }).selectOption('zh')
  await page.getByRole('heading', { name: '许可证', exact: true }).waitFor()
  await page.getByLabel('语言', { exact: true }).selectOption('en')
  await page.getByLabel('Theme', { exact: true }).selectOption('dark')
  assert.equal(await page.locator('html').getAttribute('data-theme'), 'dark')
  assert.equal(await page.evaluate(() => sessionStorage.getItem('pixels_auth_session')), token)
  console.log('PASS auth-browser/language-theme-same-session')

  await page.getByRole('button', { name: 'Operators', exact: true }).click()
  const visitor = 'browser-' + randomUUID()
  await page.getByLabel('Username', { exact: true }).fill(visitor)
  await page.getByLabel('Password (12–256 bytes)', { exact: true }).fill(password)
  const authorCreated = page.waitForResponse(r => r.url().endsWith('/api/auth/authors') && r.request().method() === 'POST')
  await page.getByRole('button', { name: 'Create', exact: true }).click()
  assert.equal((await authorCreated).status(), 201)
  await page.getByRole('button', { name: 'Sign out', exact: true }).click()
  await page.getByRole('button', { name: 'Sign in', exact: true }).waitFor()
  assert.equal((await api('/api/auth/me', 'GET', undefined, token)).status, 401)
  const visitorToken = await login(visitor)
  assert.equal(await page.getByRole('button', { name: 'Operators', exact: true }).count(), 0)
  assert.equal(await page.getByRole('button', { name: 'Issue license', exact: true }).count(), 0)
  assert.equal((await api('/api/auth/customers', 'POST', { name: 'denied', remark: '' }, visitorToken)).status, 401)
  console.log('PASS auth-browser/operator-create-visitor-denial-logout')

  await stopServer(); await startServer()
  assert.equal((await api('/api/auth/me', 'GET', undefined, visitorToken)).status, 200)
  assert.equal((await api('/api/auth/me', 'GET', undefined, token)).status, 401)
  assert.equal((await api('/api/auth/licenses/verify', 'POST', verify)).status, 401)
  console.log('PASS auth-process/restart-preserves-session-and-revocation')

  docker('stop', '--time', '10', container); stopped = true
  assert.equal((await api('/health/ready')).status, 503)
  assert.equal((await api('/health/live')).status, 204)
  assert.equal((await api('/api/auth/licenses/verify', 'POST', verify)).status, 503)
  docker('start', container); stopped = false
  const recoveryDeadline = Date.now() + 30000
  while ((await api('/health/ready')).status !== 204) {
    if (Date.now() >= recoveryDeadline) throw new Error('Auth database recovery deadline')
    await new Promise(resolve => setTimeout(resolve, 500))
  }
  assert.equal((await api('/api/auth/licenses/verify', 'POST', verify)).status, 401)
  assert.deepEqual(pageErrors, []); assert.deepEqual(external, [])
  assert.ok(!output.includes(password) && !output.includes('Synthetic private remark'))
  console.log('PASS auth-process/database-outage-fails-closed-and-recovers')
}
run().catch(error => { console.error(error.message); process.exitCode = 1 }).finally(async () => {
  if (browser) await browser.close()
  await stopServer()
  if (stopped) docker('start', container)
  const keyPath = path.join(directory, 'signing.der')
  if (fs.existsSync(keyPath)) fs.unlinkSync(keyPath)
  fs.rmdirSync(directory)
})
