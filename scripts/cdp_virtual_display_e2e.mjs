// End-to-end WebClient acceptance test for virtual displays.
// The page must expose window.__virtualDisplay (App.vue debug contract).
// Usage:
//   WEB_URL=http://host:port/web_client/?deviceId=... OUT_DIR=tests/artifacts/virtual_display_e2e \
//     node scripts/cdp_virtual_display_e2e.mjs
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const CHROME = process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const PAGE_URL = process.env.WEB_URL
const OUT_DIR = path.resolve(process.env.OUT_DIR || 'tests/artifacts/virtual_display_e2e')
const CDP_PORT = Number(process.env.CDP_PORT || 9450)
const OP_TIMEOUT_MS = Number(process.env.OP_TIMEOUT_MS || 90000)
const RECOVER_OWNED = process.env.RECOVER_OWNED === '1'
const RECOVER_ONLY = process.env.RECOVER_ONLY === '1'

if (!PAGE_URL) throw new Error('WEB_URL required')
fs.mkdirSync(OUT_DIR, { recursive: true })

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))
const profile = path.join(os.tmpdir(), `cdp-virtual-display-${Date.now()}`)
const chrome = spawn(CHROME, [
  '--headless=new',
  '--disable-gpu',
  '--disable-gpu-compositing',
  '--disable-gpu-sandbox',
  '--no-sandbox',
  '--use-angle=swiftshader',
  `--remote-debugging-port=${CDP_PORT}`,
  `--user-data-dir=${profile}`,
  '--no-first-run',
  // The acceptance Console uses the repository's self-signed HTTPS
  // certificate. A normal interactive browser keeps the user's explicit
  // certificate exception; this fresh headless profile must opt in itself so
  // topology-triggered ticket renewal exercises the real HTTPS endpoint.
  '--ignore-certificate-errors',
  '--autoplay-policy=no-user-gesture-required',
  '--window-size=1600,900',
  'about:blank',
], { stdio: ['ignore', 'ignore', 'pipe'] })

let chromeExit = null
let chromeStderr = ''
chrome.stderr?.on('data', (chunk) => {
  if (chromeStderr.length < 4000) chromeStderr += String(chunk)
})
chrome.on('exit', (code, signal) => { chromeExit = { code, signal } })

let ws
let messageId = 0
const pending = new Map()
const evidenceUrl = new URL(PAGE_URL)
for (const name of ['password', 'pwd_md5', 'token']) {
  if (evidenceUrl.searchParams.has(name)) evidenceUrl.searchParams.set(name, '[redacted]')
}
const evidence = { url: evidenceUrl.toString(), startedAt: new Date().toISOString(), stages: [] }

process.on('exit', () => {
  try { ws?.close() } catch { /* ignore */ }
  try { chrome.kill() } catch { /* ignore */ }
})

function command(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++messageId
    const timeout = setTimeout(() => {
      pending.delete(id)
      reject(new Error(`DevTools command timeout: ${method}`))
    }, 10000)
    pending.set(id, {
      resolve: (value) => { clearTimeout(timeout); resolve(value) },
      reject: (error) => { clearTimeout(timeout); reject(error) },
    })
    try {
      ws.send(JSON.stringify({ id, method, params }))
    } catch (error) {
      clearTimeout(timeout)
      pending.delete(id)
      reject(error)
    }
  })
}

async function evaluate(expression) {
  const response = await command('Runtime.evaluate', {
    expression,
    returnByValue: true,
    awaitPromise: true,
    userGesture: true,
  })
  if (response.exceptionDetails) throw new Error(JSON.stringify(response.exceptionDetails).slice(0, 600))
  return response.result?.value
}

async function waitDevtools() {
  for (let i = 0; i < 80; i++) {
    try {
      const response = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)
      if (response.ok) return
    } catch { /* retry */ }
    await sleep(250)
  }
  throw new Error(`Chrome DevTools endpoint not ready; chrome=${JSON.stringify(chromeExit)} stderr=${chromeStderr.slice(-1000)}`)
}

const STATE_JS = `(() => {
  const vd = window.__virtualDisplay?.state?.()
  const video = document.querySelector('video')
  return {
    ...(vd ?? {}),
    connectionStatus: window.__conn?.status?.() ?? 'unknown',
    loadingHint: document.querySelector('.loading-hint')?.textContent?.trim() ?? '',
    recentLogs: Array.from(document.querySelectorAll('.log-line'))
      .slice(-12)
      .map((node) => node.textContent?.trim() ?? ''),
    videoWidth: video?.videoWidth ?? 0,
    videoHeight: video?.videoHeight ?? 0,
    videoReadyState: video?.readyState ?? -1,
  }
})()`

const STATS_JS = `(async () => {
  const pc = window.__pc
  if (!pc) return { connection: 'none', inbound: [] }
  const stats = await pc.getStats()
  const inbound = []
  stats.forEach((s) => {
    if (s.type === 'inbound-rtp' && s.kind === 'video') {
      inbound.push({
        id: s.id,
        framesReceived: s.framesReceived ?? 0,
        framesDecoded: s.framesDecoded ?? 0,
        keyFramesDecoded: s.keyFramesDecoded ?? 0,
        framesPerSecond: s.framesPerSecond ?? 0,
        bytesReceived: s.bytesReceived ?? 0,
        frameWidth: s.frameWidth ?? 0,
        frameHeight: s.frameHeight ?? 0,
      })
    }
  })
  return { connection: pc.connectionState, inbound }
})()`

async function state() {
  return evaluate(STATE_JS)
}

async function clickElement(selector) {
  const result = await evaluate(`(() => {
    const element = document.querySelector(${JSON.stringify(selector)})
    if (!(element instanceof HTMLElement)) return { ok: false, reason: 'not found' }
    if (element instanceof HTMLButtonElement && element.disabled) return { ok: false, reason: 'disabled' }
    const rect = element.getBoundingClientRect()
    return { ok: true, x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 }
  })()`)
  if (!result?.ok) throw new Error(`cannot click ${selector}: ${result?.reason ?? 'unknown'}`)
  await command('Input.dispatchMouseEvent', {
    type: 'mousePressed', x: result.x, y: result.y, button: 'left', clickCount: 1,
  })
  await command('Input.dispatchMouseEvent', {
    type: 'mouseReleased', x: result.x, y: result.y, button: 'left', clickCount: 1,
  })
}

async function ensureFloatPanelControl(selector) {
  if (await evaluate(`document.querySelector(${JSON.stringify(selector)}) instanceof HTMLElement`)) return
  await clickElement('.float-ball')
  for (let i = 0; i < 20; i++) {
    if (await evaluate(`document.querySelector(${JSON.stringify(selector)}) instanceof HTMLElement`)) return
    await sleep(50)
  }
  throw new Error(`floating panel control did not appear: ${selector}`)
}

async function waitFor(label, predicate, timeout = OP_TIMEOUT_MS) {
  const deadline = Date.now() + timeout
  let last
  while (Date.now() < deadline) {
    last = await state().catch((error) => ({ error: String(error) }))
    if (predicate(last)) return last
    await sleep(500)
  }
  throw new Error(`${label} timed out; last=${JSON.stringify(last)}`)
}

async function screenshot(name) {
  const response = await command('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false })
  const output = path.join(OUT_DIR, `${name}.png`)
  fs.writeFileSync(output, Buffer.from(response.data, 'base64'))
  return output
}

async function recordStage(name) {
  const beforeStats = await evaluate(STATS_JS)
  await sleep(2500)
  const decodedBefore = beforeStats.inbound.reduce((sum, item) => sum + item.framesDecoded, 0)
  let afterStats = await evaluate(STATS_JS)
  let decodedAfter = afterStats.inbound.reduce((sum, item) => sum + item.framesDecoded, 0)
  // A monitor switch can require a fresh IDR. Keep the normal 2.5-second
  // observation window, but allow up to 10 seconds for that keyframe instead
  // of reporting a false failure just before decoding resumes.
  const decodeDeadline = Date.now() + 7500
  while (decodedAfter <= decodedBefore && Date.now() < decodeDeadline) {
    await sleep(500)
    afterStats = await evaluate(STATS_JS)
    decodedAfter = afterStats.inbound.reduce((sum, item) => sum + item.framesDecoded, 0)
  }
  const item = {
    name,
    at: new Date().toISOString(),
    state: await state(),
    stats: afterStats,
    decodedFrameDelta: decodedAfter - decodedBefore,
    screenshot: await screenshot(name),
  }
  evidence.stages.push(item)
  console.log(`[e2e] ${name}: ${JSON.stringify(item)}`)
  if (item.state.videoWidth <= 0 || item.state.videoReadyState < 2) {
    throw new Error(`${name}: video element is not rendering`)
  }
  if (item.decodedFrameDelta <= 0) {
    throw new Error(`${name}: WebRTC decoded frame count did not advance`)
  }
  return item
}

async function main() {
  await waitDevtools()
  const response = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(PAGE_URL)}`, { method: 'PUT' })
  if (!response.ok) throw new Error(`open page failed: HTTP ${response.status}`)
  const target = await response.json()
  ws = new WebSocket(target.webSocketDebuggerUrl)
  ws.onmessage = (event) => {
    const message = JSON.parse(event.data)
    if (!message.id || !pending.has(message.id)) return
    const promise = pending.get(message.id)
    pending.delete(message.id)
    message.error ? promise.reject(new Error(message.error.message)) : promise.resolve(message.result)
  }
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject })
  await command('Runtime.enable')
  await command('Page.enable')

  let baseline = await waitFor('initial WebClient stream', (s) =>
    s.connection === 'connected' && s.enabled === true && s.pending === false &&
    s.videoWidth > 0 && s.videoReadyState >= 2 && s.monitors?.length > 0)
  if (RECOVER_OWNED) {
    while (baseline.owned > 0) {
      const ownedBefore = baseline.owned
      const monitorCountBefore = baseline.monitors.length
      await ensureFloatPanelControl('[data-testid="virtual-display-remove"]')
      await clickElement('[data-testid="virtual-display-remove"]')
      baseline = await waitFor(`recovery remove owned=${ownedBefore}`, (s) =>
        s.connection === 'connected' && s.pending === false &&
        s.owned === ownedBefore - 1 && s.monitors?.length === monitorCountBefore - 1 &&
        s.videoWidth > 0 && s.videoReadyState >= 2)
    }
    if (RECOVER_ONLY) {
      evidence.finishedAt = new Date().toISOString()
      evidence.result = 'PASS'
      evidence.mode = 'RECOVER_ONLY'
      evidence.finalState = baseline
      fs.writeFileSync(path.join(OUT_DIR, 'result.json'), JSON.stringify(evidence, null, 2))
      console.log(`[e2e] RECOVERY PASS: ${path.join(OUT_DIR, 'result.json')}`)
      return
    }
  }
  const baselineOwned = baseline.owned ?? 0
  const maxOwned = baseline.max ?? 2
  if (baselineOwned >= maxOwned) {
    throw new Error(`cannot add a display at baseline: owned=${baselineOwned}, max=${maxOwned}`)
  }
  const baselineNames = baseline.monitors.map((monitor) => monitor.name)
  const baselineMonitor = baseline.capturingMonitor || baselineNames[0]
  await recordStage('01_baseline_physical')
  await ensureFloatPanelControl('[data-testid="virtual-display-add"]')
  await screenshot('01b_floating_virtual_display_controls')
  await clickElement('[data-testid="virtual-display-add"]')
  const added = await waitFor('virtual display add and WebRTC reconnect', (s) =>
    s.connection === 'connected' && s.pending === false && s.owned === baselineOwned + 1 &&
    s.monitors?.length === baselineNames.length + 1 && s.videoWidth > 0)
  const addedMonitor = added.monitors.find((monitor) => !baselineNames.includes(monitor.name))
  if (!addedMonitor) throw new Error(`new monitor not found: ${JSON.stringify(added.monitors)}`)
  await recordStage('02_after_add_physical')

  const switchToVirtual = await evaluate(`window.__virtualDisplay.switchMonitor(${JSON.stringify(addedMonitor.name)})`)
  if (!switchToVirtual) throw new Error('switch to virtual monitor request was not sent')
  await waitFor('switch to virtual monitor', (s) =>
    s.connection === 'connected' && s.capturingMonitor === addedMonitor.name && s.videoWidth > 0)
  await recordStage('03_virtual_monitor_capture')

  const switchToPhysical = await evaluate(`window.__virtualDisplay.switchMonitor(${JSON.stringify(baselineMonitor)})`)
  if (!switchToPhysical) throw new Error('switch back to physical monitor request was not sent')
  await waitFor('switch back to physical monitor', (s) =>
    s.connection === 'connected' && s.capturingMonitor === baselineMonitor && s.videoWidth > 0)
  await recordStage('04_switched_back_physical')

  await ensureFloatPanelControl('[data-testid="virtual-display-remove"]')
  await clickElement('[data-testid="virtual-display-remove"]')
  const removed = await waitFor('virtual display remove and WebRTC reconnect', (s) =>
    s.connection === 'connected' && s.pending === false && s.owned === baselineOwned &&
    s.monitors?.length === baselineNames.length &&
    !s.monitors?.some((monitor) => monitor.name === addedMonitor.name) && s.videoWidth > 0)
  if (!baselineNames.includes(removed.capturingMonitor)) {
    throw new Error(`capture did not recover to a baseline monitor: ${removed.capturingMonitor}`)
  }
  await recordStage('05_after_remove_recovered')

  evidence.finishedAt = new Date().toISOString()
  evidence.result = 'PASS'
  evidence.baselineOwned = baselineOwned
  evidence.baselineMonitorNames = baselineNames
  evidence.addedMonitor = addedMonitor
  fs.writeFileSync(path.join(OUT_DIR, 'result.json'), JSON.stringify(evidence, null, 2))
  console.log(`[e2e] PASS: ${path.join(OUT_DIR, 'result.json')}`)
}

main()
  .catch((error) => {
    evidence.finishedAt = new Date().toISOString()
    evidence.result = 'FAIL'
    evidence.error = error instanceof Error ? error.stack : String(error)
    evidence.chrome = { exit: chromeExit, stderr: chromeStderr.slice(-1000) }
    fs.writeFileSync(path.join(OUT_DIR, 'result.json'), JSON.stringify(evidence, null, 2))
    console.error('[e2e] FAIL:', error)
    process.exitCode = 1
  })
  .finally(() => {
    try { ws?.close() } catch { /* ignore */ }
    try { chrome.kill() } catch { /* ignore */ }
    setTimeout(() => process.exit(process.exitCode ?? 0), 500).unref()
  })
