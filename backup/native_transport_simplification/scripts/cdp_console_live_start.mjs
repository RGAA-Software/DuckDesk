// Console live start smoke test: use a real browser context so self-signed TLS
// behaves exactly like the Console Web. It starts a named app node and reports the
// resulting scheduler/live status without exposing Console credentials.
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'

const base = process.env.CONSOLE_URL || process.env.CMS_URL || 'https://127.0.0.1:30500'
const appName = process.env.CONSOLE_APP_NAME || process.env.CMS_APP_NAME || '1122'
const stopFirst = (process.env.CONSOLE_STOP_FIRST || process.env.CMS_STOP_FIRST) === '1'
const cdpPort = Number(process.env.CDP_PORT || 9499)
const chrome = spawn('C:/Program Files/Google/Chrome/Application/chrome.exe', [
  '--headless=new', '--ignore-certificate-errors', `--remote-debugging-port=${cdpPort}`,
  `--user-data-dir=${path.join(os.tmpdir(), `console-live-start-${Date.now()}`)}`,
  '--no-first-run', 'about:blank',
], { stdio: 'ignore' })

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))
let ws
let nextId = 0
const pending = new Map()
const command = (method, params = {}) => new Promise((resolve, reject) => {
  const id = ++nextId
  pending.set(id, { resolve, reject })
  ws.send(JSON.stringify({ id, method, params }))
})

async function evaluate(expression) {
  const result = await command('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true })
  if (result.exceptionDetails) throw new Error(result.exceptionDetails.text || 'browser evaluation failed')
  return result.result?.value
}

try {
  for (let retry = 0; retry < 60; retry += 1) {
    try { if ((await fetch(`http://127.0.0.1:${cdpPort}/json/version`)).ok) break } catch {}
    await sleep(250)
  }
  const target = await (await fetch(`http://127.0.0.1:${cdpPort}/json/new?${encodeURIComponent(base + '/')}`, { method: 'PUT' })).json()
  ws = new WebSocket(target.webSocketDebuggerUrl)
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject })
  ws.onmessage = ({ data }) => {
    const message = JSON.parse(data)
    if (message.id && pending.has(message.id)) {
      const waiter = pending.get(message.id)
      pending.delete(message.id)
      message.error ? waiter.reject(new Error(message.error.message)) : waiter.resolve(message.result)
    }
  }
  await command('Runtime.enable')
  await command('Page.enable')
  await command('Page.navigate', { url: base + '/' })
  await sleep(1200)
  const result = await evaluate(`(async () => {
    const base = ${JSON.stringify(base)}
    const rows = await (await fetch(base + '/api/v1/app/control/app/rows')).json()
    const app = (rows.data || []).find((item) => item.name === ${JSON.stringify(appName)})
    if (!app) return { ok: false, reason: 'app-not-found' }
    const node = (app.nodes || [])[0]
    if (!node) return { ok: false, reason: 'node-not-found', appId: app.app_id }
    const activeForNode = () => fetch(base + '/api/v1/app/control/app/instance/list')
      .then((response) => response.json())
      .then((instances) => (instances.data || []).find((item) => item.node_id === node.node_id && ['starting', 'running', 'stopping'].includes(item.state)))
    if (${stopFirst}) {
      const active = await activeForNode()
      if (active) {
        await fetch(base + '/api/v1/app/control/app/instance/stop/' + encodeURIComponent(active.instance_id), { method: 'POST' })
        for (let retry = 0; retry < 30; retry += 1) {
          if (!await activeForNode()) break
          await new Promise((resolve) => setTimeout(resolve, 400))
        }
      }
    }
    let active = await activeForNode()
    const start = active
      ? { code: 200, message: 'already-running' }
      : await (await fetch(base + '/api/v1/app/control/app/node/start/' + encodeURIComponent(node.node_id), { method: 'POST' })).json()
    for (let retry = 0; retry < 30; retry += 1) {
      active = await activeForNode()
      if (active?.state === 'running') break
      await new Promise((resolve) => setTimeout(resolve, 400))
    }
    const status = await (await fetch(base + '/api/v1/live/control/status?device_id=' + encodeURIComponent(node.device_id) + '&app_id=' + encodeURIComponent(app.app_id))).json()
    return {
      ok: active?.state === 'running',
      startCode: start.code,
      startMessage: start.msg || start.message || '',
      appId: app.app_id,
      nodeId: node.node_id,
      deviceId: node.device_id,
      port: node.listen_port,
      instanceState: active?.state || start.data?.state || '',
      liveOnline: status.data?.online === true,
      liveMessage: status.data?.message || '',
    }
  })()`)
  console.log(JSON.stringify(result))
  process.exitCode = result?.ok ? 0 : 1
} finally {
  try { ws?.close() } catch {}
  try { chrome.kill() } catch {}
}
