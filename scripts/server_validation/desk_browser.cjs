// Disposable native service process + actual built UI + isolated PostgreSQL.
const assert = require('node:assert/strict')
const { spawn, execFileSync } = require('node:child_process')
const { createHash, randomBytes, randomUUID } = require('node:crypto')
const { createRequire } = require('node:module')
const { once } = require('node:events')
const path = require('node:path')
const repo = path.resolve(__dirname, '../..')
const { chromium } = createRequire(path.join(repo, 'web/px_desk/package.json'))('@playwright/test')
const executable = process.argv[2]
const container = process.env.PIXELS_TEST_CONTAINER
assert.equal(process.env.PIXELS_PG_ISOLATED_TEST, '1')
assert.match(container || '', /^[a-f0-9]{12,64}$/)
const docker = (...args) => execFileSync('docker', args, { encoding: 'utf8', timeout: 45000, windowsHide: true })
const project = docker('inspect', container, '--format', '{{index .Config.Labels "com.docker.compose.project"}}').trim()
assert.match(project, /^pixels-pg-\d{8}-\d{6}-[a-f0-9]{8}$/)
const credential = randomBytes(32).toString('hex')
let child, browser, base, output = '', stopped = false

async function stopServer() {
  if (child && child.exitCode === null && child.signalCode === null) {
    const exited = once(child, 'exit')
    child.kill()
    await Promise.race([exited, new Promise((_, reject) => {
      const timer = setTimeout(() => reject(new Error('service shutdown timeout')), 10000)
      timer.unref()
    })])
  }
  child = undefined
}
async function startServer() {
  const env = { ...process.env, PIXELS_DATABASE_URL: process.env.PIXELS_TEST_DESK_RUNTIME_URL,
    PIXELS_DESK_LOCAL_DEVELOPMENT: '1', PIXELS_DESK_LISTEN: '127.0.0.1:0',
    PIXELS_DESK_ADMIN_TOKEN_SHA256: createHash('sha256').update(credential).digest('hex'),
    PIXELS_DESK_STATIC_DIRECTORY: path.join(repo, 'web/px_desk/dist') }
  delete env.PIXELS_DESK_TLS_CERT
  delete env.PIXELS_DESK_TLS_KEY
  let startup = ''
  child = spawn(executable, [], { env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] })
  base = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('service startup timeout')), 20000)
    child.once('error', reject)
    child.once('exit', () => { clearTimeout(timer); reject(new Error('service exited before readiness')) })
    child.stderr.on('data', bytes => { output += bytes.toString() })
    child.stdout.on('data', bytes => {
      output += bytes.toString()
      startup += bytes.toString()
      const match = startup.match(/Desk listening (http:\/\/127\.0\.0\.1:\d+)/)
      if (match) { clearTimeout(timer); resolve(match[1]) }
    })
  })
}
async function api(route, method = 'GET', body, token) {
  const response = await fetch(base + route, {
    method, headers: { 'content-type': 'application/json', ...(token ? { authorization: 'Bearer ' + token } : {}) },
    body: body === undefined ? undefined : JSON.stringify(body), signal: AbortSignal.timeout(22000),
  })
  const text = await response.text()
  return { status: response.status, body: text ? JSON.parse(text) : null }
}
async function run() {
  await startServer()
  assert.equal((await api('/health/ready')).status, 204)
  browser = await chromium.launch({ headless: true })
  const context = await browser.newContext()
  await context.addInitScript(() => localStorage.setItem('language', 'en'))
  await context.route('**/*', route => {
    if (new URL(route.request().url()).origin === base) return route.continue()
    return route.abort()
  })
  const page = await context.newPage()
  page.setDefaultTimeout(15000)
  const pageErrors = []
  page.on('pageerror', error => pageErrors.push(error.message))
  await page.goto(base + '/main')
  await page.getByText('Contact Us', { exact: true }).first().click()
  const dialog = page.getByRole('dialog')
  await dialog.getByLabel('Subject', { exact: true }).fill('Browser inquiry ' + randomUUID())
  await dialog.getByLabel('Your name', { exact: true }).fill('Synthetic browser')
  await dialog.locator('.el-select__wrapper').click()
  await page.getByRole('option', { name: 'Personal', exact: true }).click()
  await dialog.locator('textarea').fill('Browser private synthetic text')
  await dialog.getByLabel('Email', { exact: true }).fill('synthetic@example.invalid')
  const submitted = page.waitForResponse(r => r.url().endsWith('/api/desk/consults') && r.request().method() === 'POST')
  await dialog.getByRole('button', { name: 'Submit', exact: true }).click()
  const submission = await submitted
  assert.equal(submission.status(), 200)
  const receipt = await submission.json()
  assert.equal(Object.keys(receipt).join(','), 'id')
  console.log('PASS browser/consult-submit')

  await page.goto(base + '/main')
  await page.getByText('Submit a Ticket', { exact: true }).click()
  const issue = page.getByRole('dialog')
  const issueTitle = 'Browser ticket ' + randomUUID()
  await issue.getByLabel('Your issue', { exact: true }).fill(issueTitle)
  await issue.getByLabel('Your name', { exact: true }).fill('Synthetic browser')
  await issue.getByLabel('Details', { exact: true }).fill('Synthetic ticket')
  await issue.getByLabel('Software version', { exact: true }).fill('1.2.3')
  await issue.getByLabel('OS version', { exact: true }).fill('Windows')
  const ticketSubmitted = page.waitForResponse(r => r.url().endsWith('/api/desk/issues') && r.request().method() === 'POST')
  await issue.getByRole('button', { name: 'Submit', exact: true }).click()
  assert.equal((await ticketSubmitted).status(), 200)
  console.log('PASS browser/issue-submit')

  await page.goto(base + '/admin')
  await page.getByPlaceholder('Enter the 64-character administrator token').fill(credential)
  await page.getByRole('button', { name: 'Sign In', exact: true }).click()
  await page.waitForURL('**/admin/panel')
  const session = await page.evaluate(() => sessionStorage.getItem('pixels_desk_session'))
  assert.ok(session && session !== credential)
  await page.getByRole('button', { name: 'Tickets', exact: true }).click()
  const row = page.getByRole('row').filter({ hasText: issueTitle })
  const marked = page.waitForResponse(r => r.request().method() === 'PATCH')
  await row.getByRole('button', { name: 'Mark Processed', exact: true }).click()
  assert.equal((await marked).status(), 200)
  await row.getByRole('button', { name: 'Reopen', exact: true }).waitFor()
  await page.getByRole('button', { name: 'Sign Out', exact: true }).click()
  await page.waitForURL('**/admin')
  assert.equal((await api('/api/desk/consults?page=1&page_size=100', 'GET', undefined, session)).status, 401)
  assert.deepEqual(pageErrors, [])
  console.log('PASS browser/login-mark-logout-revokes')
  await browser.close()
  browser = undefined

  await stopServer()
  await startServer()
  const login = await api('/api/desk/admin/sessions', 'POST', { token: credential })
  assert.equal(login.status, 200)
  const rows = await api('/api/desk/consults?page=1&page_size=100', 'GET', undefined, login.body.access_token)
  assert.ok(rows.body.items.some(row => row.id === receipt.id))
  assert.equal((await api('/api/desk/consults?page=1&page_size=100', 'GET', undefined, session)).status, 401)
  console.log('PASS process/restart-preserves-data-and-revocation')

  docker('stop', '--time', '5', container)
  stopped = true
  assert.equal((await api('/health/ready')).status, 503)
  assert.equal((await api('/health/live')).status, 204)
  const body = { request_id: randomUUID(), title: 'outage', your_name: 'test', description: 'must not acknowledge',
    email: '', wechat: '', qq: '', consult_type: 'personal' }
  assert.equal((await api('/api/desk/consults', 'POST', body)).status, 503)
  docker('start', container)
  stopped = false
  const deadline = Date.now() + 30000
  while ((await api('/health/ready')).status !== 204) {
    assert.ok(Date.now() < deadline, 'database did not recover')
    await new Promise(resolve => setTimeout(resolve, 100))
  }
  const recovered = await api('/api/desk/consults?page=1&page_size=100', 'GET', undefined, login.body.access_token)
  assert.ok(recovered.body.items.every(row => row.id !== body.request_id))
  assert.equal((await api('/api/desk/consults', 'POST', body)).status, 200)
  assert.ok(!output.includes(credential) && !output.includes('Browser private synthetic text'))
  console.log('PASS process/database-outage-503-and-recovery')
}
run().catch(error => { console.error(error.message); process.exitCode = 1 }).finally(async () => {
  if (browser) await browser.close()
  await stopServer()
  if (stopped) docker('start', container)
})
