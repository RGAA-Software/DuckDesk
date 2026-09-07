// End-to-end WebView input/stability smoke test.
// Starts a public WebView application, connects with input permission, sends
// real CDP mouse/keyboard events through the Web client and keeps the instance
// alive long enough to catch the historical ~20 second libcef crash.
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const consoleBase = process.env.PX_CONSOLE_TEST_BASE_URL || process.env.PX_CMS_TEST_BASE_URL || 'https://127.0.0.1:30500'
const APP_NAME = process.env.PX_WEBVIEW_TEST_APP || 'baidu'
const OBSERVE_MS = Number(process.env.PX_WEBVIEW_TEST_OBSERVE_MS || 120_000)
const INPUT_PHASE = process.env.PX_WEBVIEW_TEST_INPUT_PHASE || 'full'
const PHASE_WAIT_MS = Number(process.env.PX_WEBVIEW_TEST_PHASE_WAIT_MS || 10_000)
const ENTER_WAIT_MS = Number(process.env.PX_WEBVIEW_TEST_ENTER_WAIT_MS || PHASE_WAIT_MS)
const PRIMARY_X = Number(process.env.PX_WEBVIEW_TEST_PRIMARY_X || 0.5)
const PRIMARY_Y = Number(process.env.PX_WEBVIEW_TEST_PRIMARY_Y || 0.30)
const CDP_PORT = Number(process.env.PX_WEBVIEW_TEST_CDP_PORT || 9521)
const SCREENSHOT_PATH = process.env.PX_WEBVIEW_TEST_SCREENSHOT_PATH || ''
const RTC_ROUTE = process.env.PX_WEBRTC_TEST_ROUTE || 'auto'
const FORCE_RELAY = process.env.PX_WEBRTC_FORCE_RELAY === '1'
const EXPECT_RELAY_PROTOCOL = (process.env.PX_WEBRTC_EXPECT_RELAY_PROTOCOL || '').trim().toLowerCase()
const EXPECT_RTC_MODE = (process.env.PX_WEBRTC_EXPECT_MODE || '').trim().toLowerCase()
const HOLD_MS = Math.max(0, Number(process.env.PX_WEBRTC_HOLD_MS || 0))
const EXPECT_MIN_REVISION = Math.max(0, Number(process.env.PX_WEBRTC_EXPECT_MIN_REVISION || 0))
const TEST_USER = process.env.PX_WEBVIEW_TEST_USER || ''
const TEST_PASSWORD = process.env.PX_WEBVIEW_TEST_PASSWORD || ''
const TEST_PERMISSIONS = (process.env.PX_WEBVIEW_TEST_PERMISSIONS || 'view,input')
  .split(',').map((value) => value.trim()).filter(Boolean)
const TEST_DEVICE_ID = (process.env.PX_WEBRTC_TEST_DEVICE_ID || '').trim()
const FULL_FEATURES = process.env.PX_WEBRTC_FULL_FEATURES === '1'
const REQUIRE_CLIPBOARD_ECHO = process.env.PX_WEBRTC_REQUIRE_CLIPBOARD_ECHO === '1'
const RESET_VIRTUAL_DISPLAYS = process.env.PX_WEBRTC_RESET_VIRTUAL_DISPLAYS === '1'
const TEST_MIC = process.env.PX_WEBRTC_TEST_MIC === '1'
const VOICE_ONLY = process.env.PX_WEBRTC_VOICE_ONLY === '1'
const TEST_RECONNECT = process.env.PX_WEBRTC_TEST_RECONNECT === '1'
const SECURE_TEST_ORIGIN = (process.env.PX_WEBRTC_SECURE_TEST_ORIGIN || '').trim()
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

const profile = path.join(os.tmpdir(), `px-webview-e2e-${Date.now()}`)
const chromeArgs = [
  '--headless=new',
  '--ignore-certificate-errors',
  `--remote-debugging-port=${CDP_PORT}`,
  `--user-data-dir=${profile}`,
  '--no-first-run',
  ...(TEST_MIC ? [
    '--autoplay-policy=no-user-gesture-required',
    '--use-fake-device-for-media-stream',
    '--use-fake-ui-for-media-stream',
  ] : []),
  ...(TEST_MIC && SECURE_TEST_ORIGIN
    ? [`--unsafely-treat-insecure-origin-as-secure=${SECURE_TEST_ORIGIN}`]
    : []),
  'about:blank',
]
const chrome = spawn(CHROME, chromeArgs, { stdio: 'ignore' })

let ws
let id = 0
let launchedInstanceId = ''
let launchedCsrf = ''
let launchedAsUser = false
const pending = new Map()
const command = (method, params = {}) => new Promise((resolve, reject) => {
  const requestId = ++id
  const timer = setTimeout(() => {
    pending.delete(requestId)
    reject(new Error(`CDP ${method} timed out`))
  }, 15_000)
  pending.set(requestId, { resolve, reject, timer })
  ws.send(JSON.stringify({ id: requestId, method, params }))
})
const evaluate = async (expression) => {
  const response = await command('Runtime.evaluate', {
    expression,
    returnByValue: true,
    awaitPromise: true,
    userGesture: true,
  })
  if (response.exceptionDetails) {
    throw new Error(
      response.exceptionDetails.exception?.description
      || response.exceptionDetails.text
      || 'browser evaluation failed',
    )
  }
  return response.result?.value
}

async function waitForCdp() {
  for (let retry = 0; retry < 80; retry += 1) {
    try {
      if ((await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).ok) return
    } catch { /* retry */ }
    await sleep(250)
  }
  throw new Error('Chrome DevTools endpoint did not start')
}

async function sendClick(x, y) {
  await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y })
  await command('Input.dispatchMouseEvent', {
    type: 'mousePressed', x, y, button: 'left', clickCount: 1,
  })
  await command('Input.dispatchMouseEvent', {
    type: 'mouseReleased', x, y, button: 'left', clickCount: 1,
  })
}

async function sendText(text) {
  for (const value of text) {
    await command('Input.dispatchKeyEvent', { type: 'keyDown', text: value, key: value })
    await command('Input.dispatchKeyEvent', { type: 'keyUp', key: value })
  }
}

async function requireStream(label, waitMs = PHASE_WAIT_MS) {
  await sleep(waitMs)
  const state = await evaluate(`(() => {
    const element = document.querySelector('video')
    return {
      readyState: element?.readyState || 0,
      frames: element?.getVideoPlaybackQuality?.().totalVideoFrames || 0,
      connection: window.__conn?.status?.() || '',
    }
  })()`).catch(() => null)
  if (!state || state.readyState < 2 || state.connection === 'closed' || state.connection === 'failed') {
    throw new Error(`${label} lost WebView connection: ${JSON.stringify(state)}`)
  }
  console.log(`PHASE ${label} stable ${JSON.stringify(state)}`)
}

async function captureRemoteFrame(filePath) {
  if (!filePath) return
  const dataUrl = await evaluate(`(() => {
    const video = document.querySelector('video')
    if (!video || video.readyState < 2) return ''
    const canvas = document.createElement('canvas')
    canvas.width = video.videoWidth
    canvas.height = video.videoHeight
    canvas.getContext('2d').drawImage(video, 0, 0)
    return canvas.toDataURL('image/png')
  })()`)
  if (!dataUrl) throw new Error('could not capture remote video frame')
  fs.writeFileSync(filePath, Buffer.from(dataUrl.replace(/^data:image\/png;base64,/, ''), 'base64'))
  console.log(`SCREENSHOT ${filePath}`)
}

async function testStandardRtcFeatures() {
  const audio = await evaluate(`window.__mic?.state?.() || null`)
  if (!audio || audio.systemAudioTrackCount < 1) {
    throw new Error(`standard RTC system-audio track missing: ${JSON.stringify(audio)}`)
  }
  console.log(`FEATURE audio ${JSON.stringify(audio)}`)

  if (TEST_MIC) {
    await evaluate(`(() => { void window.__mic.toggle(); return true })()`)
    let preflight = null
    for (let retry = 0; retry < 40; retry += 1) {
      preflight = await evaluate(`window.__mic?.state?.() || null`).catch(() => null)
      if (preflight?.preflightPending) break
      await sleep(100)
    }
    if (!preflight?.preflightPending || preflight.systemOutputMuted !== true) {
      throw new Error(`voice preflight/pause-remote-audio requirement missing: ${JSON.stringify(preflight)}`)
    }
    const warning = await evaluate(`document.querySelector('.el-message-box__message')?.textContent || ''`)
    const hasPauseWarning = (warning.includes('暂停') && warning.includes('声音'))
      || (warning.toLowerCase().includes('pause') && warning.toLowerCase().includes('remote sound'))
    if (!hasPauseWarning) {
      throw new Error(`voice preflight warning is incomplete: ${warning}`)
    }
    const confirmed = await evaluate(`(() => {
      const buttons = Array.from(document.querySelectorAll('.el-message-box__btns button'))
      const button = buttons.find((item) => item.classList.contains('el-button--primary'))
      if (!button) return false
      button.click()
      return true
    })()`)
    if (!confirmed) throw new Error('voice preflight confirmation button missing')
    let voice = null
    for (let retry = 0; retry < 160; retry += 1) {
      voice = await evaluate(`window.__mic?.state?.() || null`).catch(() => null)
      if (voice?.phase === 'connected' || voice?.phase === 'error') break
      await sleep(250)
    }
    if (voice?.phase !== 'connected' || voice.senderTrack !== 'live') {
      throw new Error(`voice call did not connect: ${JSON.stringify(voice)}`)
    }
    const bytesBefore = await evaluate(`(async () => {
      let bytes = 0
      ;(await window.__pc.getStats()).forEach((s) => {
        if (s.type === 'outbound-rtp' && (s.kind === 'audio' || s.mediaType === 'audio')) bytes += s.bytesSent || 0
      })
      return bytes
    })()`)
    await sleep(3000)
    const bytesAfter = await evaluate(`(async () => {
      let bytes = 0
      ;(await window.__pc.getStats()).forEach((s) => {
        if (s.type === 'outbound-rtp' && (s.kind === 'audio' || s.mediaType === 'audio')) bytes += s.bytesSent || 0
      })
      return bytes
    })()`)
    if (bytesAfter <= bytesBefore) {
      throw new Error(`voice audio did not flow: ${bytesBefore} -> ${bytesAfter}`)
    }
    await evaluate(`window.__mic.toggle()`)
    console.log(`FEATURE voice-call outbound-bytes=${bytesBefore}->${bytesAfter}`)
  }

  if (VOICE_ONLY) return

  const clipboardText = `px-standard-rtc-${Date.now()}`
  const clipboardSent = await evaluate(`window.__clipboard?.sendText?.(${JSON.stringify(clipboardText)}) || false`)
  if (!clipboardSent) throw new Error('standard RTC clipboard send failed')
  let clipboardEcho = ''
  let clipboardAck = ''
  for (let retry = 0; retry < 40; retry += 1) {
    clipboardEcho = await evaluate(`window.__clipboard?.lastRemote?.() || ''`).catch(() => '')
    clipboardAck = await evaluate(`window.__clipboard?.lastAck?.() || ''`).catch(() => '')
    if (clipboardAck === clipboardText || clipboardEcho === clipboardText) break
    await sleep(250)
  }
  if (clipboardAck === clipboardText) {
    console.log(`FEATURE clipboard remote-write-ack chars=${clipboardText.length}`)
  } else if (clipboardEcho === clipboardText) {
    console.log(`FEATURE clipboard echo chars=${clipboardText.length}`)
  } else if (REQUIRE_CLIPBOARD_ECHO) {
    throw new Error(`standard RTC clipboard acknowledgement failed: ${JSON.stringify({ clipboardAck, clipboardEcho })}`)
  } else {
    console.log(`FEATURE clipboard outbound chars=${clipboardText.length} echo=pending-remote-verification`)
  }

  let ftReady = false
  for (let retry = 0; retry < 80; retry += 1) {
    ftReady = await evaluate(`Boolean(window.__ft?.ready?.() && window.__ft?.supported?.())`).catch(() => false)
    if (ftReady) break
    await sleep(250)
  }
  if (!ftReady) throw new Error('standard RTC file-transfer channel/protocol not ready')
  const fileName = `px_rtc_e2e_${Date.now()}.txt`
  const remoteDir = 'C:\\Users\\Public\\Documents'
  const remotePath = `${remoteDir}\\${fileName}`
  const content = `Pixels standard RTC file round trip ${Date.now()}\n`
  const upload = await evaluate(`window.__ft.uploadText(${JSON.stringify(fileName)}, ${JSON.stringify(remoteDir)}, ${JSON.stringify(content)})`)
  let uploadJob = null
  for (let retry = 0; retry < 120; retry += 1) {
    uploadJob = await evaluate(`(window.__ft?.jobs?.() || []).find((job) => job.id === ${Number(upload.jobId)}) || null`).catch(() => null)
    if (uploadJob?.state === 'done' || uploadJob?.state === 'error' || uploadJob?.state === 'cancelled') break
    await sleep(250)
  }
  if (uploadJob?.state !== 'done') throw new Error(`standard RTC upload failed: ${JSON.stringify(uploadJob)}`)
  const download = await evaluate(`window.__ft.download(${JSON.stringify(remotePath)})`)
  if (!/^[a-f0-9]{64}$/i.test(upload.sha256)
      || !/^[a-f0-9]{64}$/i.test(download.sha256)
      || download.sha256 !== upload.sha256
      || download.size !== upload.size) {
    throw new Error(`standard RTC file hash mismatch: ${JSON.stringify({ upload, download })}`)
  }
  await evaluate(`window.__ft.removeFile(${JSON.stringify(remotePath)})`)
  console.log(`FEATURE file roundtrip bytes=${download.size} sha256=${download.sha256}`)

  let beforeDisplay = await evaluate(`window.__virtualDisplay?.state?.() || null`)
  if (!beforeDisplay?.enabled) throw new Error(`virtual display unavailable: ${JSON.stringify(beforeDisplay)}`)
  if (RESET_VIRTUAL_DISPLAYS) {
    while (beforeDisplay.owned > 0) {
      const previousOwned = beforeDisplay.owned
      const sent = await evaluate(`window.__virtualDisplay.remove()`)
      if (sent === false) throw new Error(`virtual display reset command rejected: ${JSON.stringify(beforeDisplay)}`)
      let resetState = null
      for (let retry = 0; retry < 160; retry += 1) {
        resetState = await evaluate(`window.__virtualDisplay?.state?.() || null`).catch(() => null)
        if (resetState
            && !resetState.pending
            && resetState.connection === 'connected'
            && resetState.owned < previousOwned) break
        await sleep(250)
      }
      if (!resetState || resetState.pending || resetState.owned >= previousOwned) {
        throw new Error(`virtual display reset failed: ${JSON.stringify(resetState)}`)
      }
      beforeDisplay = resetState
      console.log(`FEATURE virtual-display reset owned=${beforeDisplay.owned}`)
    }
  }
  let createdDisplay = false
  try {
    const sent = await evaluate(`window.__virtualDisplay.create()`)
    if (sent === false) throw new Error('virtual display create command was rejected')
    let afterCreate = null
    for (let retry = 0; retry < 80; retry += 1) {
      afterCreate = await evaluate(`window.__virtualDisplay?.state?.() || null`).catch(() => null)
      // The driver reports its owned count before Windows finishes publishing
      // the new monitor into the capture topology. Do not mistake that
      // intermediate state for a completed create.
      if (afterCreate
          && !afterCreate.pending
          && afterCreate.connection === 'connected'
          && afterCreate.owned > beforeDisplay.owned
          && afterCreate.monitors.length > beforeDisplay.monitors.length) break
      await sleep(250)
    }
    if (!afterCreate
        || afterCreate.pending
        || afterCreate.owned <= beforeDisplay.owned
        || afterCreate.monitors.length <= beforeDisplay.monitors.length) {
      throw new Error(`virtual display did not appear: ${JSON.stringify(afterCreate)}`)
    }
    createdDisplay = true
    const oldNames = new Set((beforeDisplay.monitors || []).map((item) => item.name))
    const added = (afterCreate.monitors || []).find((item) => !oldNames.has(item.name))
    if (!added) throw new Error(`new virtual monitor was not enumerated: ${JSON.stringify(afterCreate)}`)
    await evaluate(`window.__virtualDisplay.switchMonitor(${JSON.stringify(added.name)})`)
    let switched = false
    for (let retry = 0; retry < 60; retry += 1) {
      const state = await evaluate(`window.__virtualDisplay?.state?.() || null`).catch(() => null)
      if (state?.capturingMonitor === added.name) { switched = true; break }
      await sleep(250)
    }
    if (!switched) throw new Error(`virtual monitor switch failed: ${added.name}`)
    await requireStream('virtual-display-switch', 3000)
    console.log(`FEATURE multi-display created-and-switched name=${added.name}`)
  } finally {
    if (createdDisplay) {
      await evaluate(`window.__virtualDisplay.remove()`).catch(() => false)
      for (let retry = 0; retry < 80; retry += 1) {
        const state = await evaluate(`window.__virtualDisplay?.state?.() || null`).catch(() => null)
        if (state && state.owned <= beforeDisplay.owned) break
        await sleep(250)
      }
    }
  }
}

async function main() {
  await waitForCdp()
  const target = await (await fetch(
    `http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(consoleBase + '/')}`,
    { method: 'PUT' },
  )).json()
  ws = new WebSocket(target.webSocketDebuggerUrl)
  ws.onmessage = ({ data }) => {
    const message = JSON.parse(data)
    if (message.method === 'Runtime.consoleAPICalled') {
      const values = (message.params?.args || []).map((arg) => arg.value ?? arg.description).filter(Boolean)
      if (values.length) console.log(`BROWSER ${values.join(' ')}`)
      return
    }
    if (message.method === 'Runtime.exceptionThrown') {
      console.log(`BROWSER_EXCEPTION ${message.params?.exceptionDetails?.exception?.description || message.params?.exceptionDetails?.text || ''}`)
      return
    }
    if (!message.id || !pending.has(message.id)) return
    const waiter = pending.get(message.id)
    pending.delete(message.id)
    clearTimeout(waiter.timer)
    message.error ? waiter.reject(new Error(message.error.message)) : waiter.resolve(message.result)
  }
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject })
  await command('Runtime.enable')
  await command('Page.enable')
  if (FORCE_RELAY) {
    await command('Page.addScriptToEvaluateOnNewDocument', { source: `(() => {
      const NativePeerConnection = window.RTCPeerConnection
      function RelayOnlyPeerConnection(configuration = {}) {
        return new NativePeerConnection({ ...configuration, iceTransportPolicy: 'relay' })
      }
      RelayOnlyPeerConnection.prototype = NativePeerConnection.prototype
      Object.setPrototypeOf(RelayOnlyPeerConnection, NativePeerConnection)
      window.RTCPeerConnection = RelayOnlyPeerConnection
    })()` })
  }
  await command('Storage.getCookies')

  let originReady = false
  for (let retry = 0; retry < 60; retry += 1) {
    originReady = await evaluate(`location.origin === ${JSON.stringify(new URL(consoleBase).origin)}`)
      .catch(() => false)
    if (originReady) break
    await sleep(250)
  }
  if (!originReady) throw new Error('Console origin did not load')

  const launch = await evaluate(`(async () => {
    const json = (url, options) => Promise.race([
      fetch(url, options).then((response) => response.json()),
      new Promise((_, reject) => setTimeout(() => reject(new Error('request timed out: ' + url)), 10000)),
    ])
    const nonce = globalThis.crypto?.randomUUID?.()
      || ('rtc-e2e-' + Date.now() + '-' + Math.random().toString(16).slice(2))
    const username = ${JSON.stringify(TEST_USER)}
    const password = ${JSON.stringify(TEST_PASSWORD)}
    const authenticated = Boolean(username)
    const session = authenticated
      ? await json('/api/v1/session/user/login', {
          method: 'POST', credentials: 'same-origin',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ username, password, client_type: 'user_web' }),
        })
      : await json('/api/v1/session/guest', {
          method: 'POST', credentials: 'same-origin',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ client_nonce: nonce }),
        })
    const csrf = session.data?.csrf_token
    if (!csrf) return { error: authenticated ? 'user-session-failed' : 'guest-session-failed', session }
    const apiRoot = authenticated ? '/api/v1/user' : '/api/v1/public'
    const requestedPermissions = ${JSON.stringify(TEST_PERMISSIONS)}
    const requestedDeviceId = ${JSON.stringify(TEST_DEVICE_ID)}
    if (requestedDeviceId) {
      if (!authenticated) return { error: 'device-ticket-requires-user' }
      const devices = await json('/api/v1/user/devices', { credentials: 'same-origin' })
      const device = (devices.data || []).find((item) => item.device_id === requestedDeviceId)
      if (!device) return { error: 'device-not-granted', devices: (devices.data || []).map((item) => item.device_id) }
      const ticket = await json('/api/v1/user/devices/' + encodeURIComponent(requestedDeviceId) + '/ticket', {
        method: 'POST', credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf },
        body: JSON.stringify({ client_nonce: nonce, requested_permissions: requestedPermissions }),
      })
      if (!ticket.data?.launch_url) return { error: 'device-ticket-failed', ticket }
      const url = new URL(ticket.data.launch_url, location.href)
      const rtc = ticket.data.rtc_ice_config
      const route = ${JSON.stringify(RTC_ROUTE)}
      url.searchParams.set('connType', route === 'standard' ? 'rtc' : route === 'direct' ? 'rtc_direct' : (rtc?.direct_probe_enabled ? 'rtc_direct' : 'rtc'))
      const fragment = new URLSearchParams(url.hash.replace(/^#/, ''))
      fragment.set('renew_url', location.origin + '/api/v1/connection-tickets/renew')
      if (ticket.data.renewal_token) fragment.set('renew', ticket.data.renewal_token)
      fragment.set('perms', requestedPermissions.join(','))
      if (ticket.data.relay_host) fragment.set('relay_host', ticket.data.relay_host)
      if (ticket.data.relay_port) fragment.set('relay_port', String(ticket.data.relay_port))
      if (ticket.data.signal_device_id) fragment.set('signal_device_id', ticket.data.signal_device_id)
      if (rtc) {
        const bytes = new TextEncoder().encode(JSON.stringify(rtc)); let binary = ''
        for (const byte of bytes) binary += String.fromCharCode(byte)
        fragment.set('ice', btoa(binary).split('+').join('-').split('/').join('_').replace(/=+$/g, ''))
      }
      url.hash = fragment.toString()
      return { url: url.toString(), instanceId: '', csrf, appId: 'device:' + requestedDeviceId, authenticated }
    }
    const catalog = await json(apiRoot + '/apps', { credentials: 'same-origin' })
    const app = (catalog.data || []).find((item) => item.name === ${JSON.stringify(APP_NAME)})
    if (!app) return { error: 'app-not-found', names: (catalog.data || []).map((item) => item.name) }
    const start = await json(apiRoot + '/apps/' + encodeURIComponent(app.app_id) + '/start', {
      method: 'POST', credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf },
      body: JSON.stringify({ client_nonce: nonce }),
    })
    const instanceId = start.data?.instance_id
    if (!instanceId) return { error: 'start-failed', start }
    let instance = start.data
    for (let retry = 0; retry < 50; retry += 1) {
      const list = await json(apiRoot + '/instances', { credentials: 'same-origin' })
      instance = (list.data || []).find((item) => item.instance_id === instanceId) || instance
      if (instance.state === 'running' || instance.state === 'failed' || instance.state === 'stopped') break
      await new Promise((resolve) => setTimeout(resolve, 400))
    }
    if (instance.state !== 'running') return { error: 'instance-not-running', instance }
    const ticket = await json(apiRoot + '/instances/' + encodeURIComponent(instanceId) + '/ticket', {
      method: 'POST', credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf },
      body: JSON.stringify({ client_nonce: nonce, requested_permissions: requestedPermissions }),
    })
    if (!ticket.data?.launch_url) return { error: 'ticket-failed', ticket }
    const url = new URL(ticket.data.launch_url, location.href)
    const rtc = ticket.data.rtc_ice_config
    const route = ${JSON.stringify(RTC_ROUTE)}
    url.searchParams.set(
      'connType',
      route === 'standard' ? 'rtc'
        : route === 'direct' ? 'rtc_direct'
          : (rtc?.direct_probe_enabled ? 'rtc_direct' : 'rtc'),
    )
    const fragment = new URLSearchParams(url.hash.replace(/^#/, ''))
    fragment.set('renew_url', location.origin + '/api/v1/connection-tickets/renew')
    if (ticket.data.renewal_token) fragment.set('renew', ticket.data.renewal_token)
    fragment.set('perms', requestedPermissions.join(','))
    if (ticket.data.relay_host) fragment.set('relay_host', ticket.data.relay_host)
    if (ticket.data.relay_port) fragment.set('relay_port', String(ticket.data.relay_port))
    if (ticket.data.signal_device_id) fragment.set('signal_device_id', ticket.data.signal_device_id)
    if (rtc) {
      const bytes = new TextEncoder().encode(JSON.stringify(rtc))
      let binary = ''
      for (const byte of bytes) binary += String.fromCharCode(byte)
      fragment.set('ice', btoa(binary).split('+').join('-').split('/').join('_').replace(/=+$/g, ''))
    }
    url.hash = fragment.toString()
    return { url: url.toString(), instanceId, csrf, appId: app.app_id, authenticated }
  })()`)
  if (launch.error) throw new Error(`launch failed: ${JSON.stringify(launch)}`)
  launchedInstanceId = launch.instanceId
  launchedCsrf = launch.csrf
  launchedAsUser = Boolean(launch.authenticated)
  console.log(`START instance=${launch.instanceId} app=${launch.appId}`)

  await command('Page.navigate', { url: launch.url })
  let video = null
  for (let retry = 0; retry < 120; retry += 1) {
    video = await evaluate(`(() => {
      const element = document.querySelector('video')
      if (!element) return null
      const rect = element.getBoundingClientRect()
      return {
        readyState: element.readyState,
        width: element.videoWidth,
        height: element.videoHeight,
        x: rect.left,
        y: rect.top,
        displayWidth: rect.width,
        displayHeight: rect.height,
        connection: window.__conn?.status?.() || '',
        logs: Array.from(document.querySelectorAll('.log-line')).slice(-30).map((item) => item.textContent),
      }
    })()`).catch(() => null)
    if (video?.readyState >= 2 && video.width > 0 && video.height > 0) break
    await sleep(500)
  }
  if (!video || video.readyState < 2) throw new Error(`video did not start: ${JSON.stringify(video)}`)
  console.log(`CONNECTED ${video.width}x${video.height} status=${video.connection}`)
  await sleep(2_500)
  const rtc = await evaluate(`({
    mode: window.__conn?.rtcMode?.() || '',
    revision: window.__conn?.iceRevision?.() || 0,
    reconnects: window.__conn?.reconnectCount?.() || 0,
    selectedPath: window.__conn?.selectedPath?.() || null,
  })`)
  console.log(`RTC ${JSON.stringify(rtc)}`)
  if (EXPECT_RTC_MODE && rtc?.mode !== EXPECT_RTC_MODE) {
    throw new Error(`expected RTC mode ${EXPECT_RTC_MODE}: ${JSON.stringify(rtc)}`)
  }
  if (FORCE_RELAY && !`${rtc?.selectedPath?.local || ''} ${rtc?.selectedPath?.remote || ''}`.includes('relay')) {
    throw new Error(`forced relay selected a non-relay path: ${JSON.stringify(rtc)}`)
  }
  if (EXPECT_RELAY_PROTOCOL &&
      !`${rtc?.selectedPath?.local || ''} ${rtc?.selectedPath?.remote || ''}`.toLowerCase()
        .includes(`turn:${EXPECT_RELAY_PROTOCOL}`)) {
    throw new Error(`expected TURN/${EXPECT_RELAY_PROTOCOL.toUpperCase()} path: ${JSON.stringify(rtc)}`)
  }
  if (HOLD_MS > 0) {
    const framesBefore = await evaluate(`document.querySelector('video')?.getVideoPlaybackQuality?.().totalVideoFrames || 0`)
    console.log(`HOLD active-session ${HOLD_MS}ms revision=${rtc?.revision || 0} frames=${framesBefore}`)
    await sleep(HOLD_MS)
    const afterHold = await evaluate(`({
      mode: window.__conn?.rtcMode?.() || '',
      revision: window.__conn?.iceRevision?.() || 0,
      reconnects: window.__conn?.reconnectCount?.() || 0,
      status: window.__conn?.status?.() || '',
      frames: document.querySelector('video')?.getVideoPlaybackQuality?.().totalVideoFrames || 0,
      selectedPath: window.__conn?.selectedPath?.() || null,
    })`)
    console.log(`RTC_AFTER_HOLD ${JSON.stringify(afterHold)}`)
    if (afterHold.status !== 'connected' || afterHold.frames <= framesBefore) {
      throw new Error(`active RTC session was not stable during hold: ${JSON.stringify(afterHold)}`)
    }
    if (EXPECT_MIN_REVISION && afterHold.revision < EXPECT_MIN_REVISION) {
      throw new Error(`RTC revision did not update to ${EXPECT_MIN_REVISION}: ${JSON.stringify(afterHold)}`)
    }
  }

  let inputReady = false
  for (let retry = 0; retry < 60; retry += 1) {
    inputReady = await evaluate(`Boolean(window.__input?.attached?.())`).catch(() => false)
    if (inputReady) break
    await sleep(250)
  }
  if (!inputReady) throw new Error('Web client input channel did not attach')
  console.log('INPUT attached')
  if (FULL_FEATURES) await testStandardRtcFeatures()
  if (TEST_RECONNECT) {
    const reconnectBefore = await evaluate(`window.__conn?.reconnectCount?.() || 0`)
    await evaluate(`(() => {
      window.__e2ePreviousPc = window.__pc
      window.__pc?.close()
      return true
    })()`)
    let reconnected = null
    for (let retry = 0; retry < 200; retry += 1) {
      reconnected = await evaluate(`({
        status: window.__conn?.status?.() || '',
        reconnects: window.__conn?.reconnectCount?.() || 0,
        frames: document.querySelector('video')?.getVideoPlaybackQuality?.().totalVideoFrames || 0,
        replaced: Boolean(window.__pc && window.__pc !== window.__e2ePreviousPc),
      })`).catch(() => null)
      if (reconnected?.status === 'connected'
          && reconnected.replaced
          && reconnected.frames > 0) break
      await sleep(250)
    }
    if (!reconnected
        || reconnected.status !== 'connected'
        || !reconnected.replaced
        || reconnected.frames <= 0) {
      throw new Error(`standard RTC reconnect failed: ${JSON.stringify(reconnected)}`)
    }
    console.log(`FEATURE reconnect peer-replaced=true retry-counter=${reconnectBefore}->${reconnected.reconnects} frames=${reconnected.frames}`)
  }

  // Baidu's search box is near the upper-center of the remote page. Input is
  // sent through the real Web client listeners, not injected into the CEF DOM.
  const contentX = video.x + video.displayWidth * PRIMARY_X
  const contentY = video.y + video.displayHeight * PRIMARY_Y
  await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: contentX, y: contentY })
  await requireStream('mouse-move')
  if (INPUT_PHASE === 'move') return

  await sendClick(contentX, contentY)
  const firstMouse = await evaluate(`window.__input?.lastMouse?.() || null`)
  if (!firstMouse?.released) throw new Error(`mouse event was not forwarded: ${JSON.stringify(firstMouse)}`)
  const focusedInputSink = await evaluate(`(() => {
    const active = document.activeElement
    return active?.tagName === 'TEXTAREA' && active?.getAttribute('aria-hidden') === 'true'
  })()`)
  if (!focusedInputSink) throw new Error('Web client text input sink did not retain focus after video click')
  console.log(`INPUT mouse ${JSON.stringify(firstMouse)}`)
  console.log('INPUT text sink focused')
  await requireStream('mouse-click')
  if (INPUT_PHASE === 'click') {
    await captureRemoteFrame(SCREENSHOT_PATH)
    return
  }

  await evaluate(`(() => {
    window.__pxTestInputValues = []
    document.activeElement?.addEventListener('input', (event) => {
      window.__pxTestInputValues.push(event.target?.value || '')
    })
  })()`)
  await sendText('OpenAI')
  const localInputValues = await evaluate(`window.__pxTestInputValues || []`)
  if (!localInputValues.length) throw new Error('Chrome did not emit text input events into the Web client sink')
  console.log(`INPUT local text events ${JSON.stringify(localInputValues)}`)
  await requireStream('text')
  await captureRemoteFrame(SCREENSHOT_PATH)
  if (INPUT_PHASE === 'text') return

  await command('Input.dispatchKeyEvent', {
    type: 'keyDown', key: 'ArrowDown', code: 'ArrowDown', windowsVirtualKeyCode: 40,
  })
  await command('Input.dispatchKeyEvent', {
    type: 'keyUp', key: 'ArrowDown', code: 'ArrowDown', windowsVirtualKeyCode: 40,
  })
  await requireStream('physical-key')
  if (INPUT_PHASE === 'key') return

  await command('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 })
  await command('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 })
  await requireStream('enter', ENTER_WAIT_MS)
  if (INPUT_PHASE === 'enter') return

  // Click the first result. It normally uses target=_blank and therefore
  // exercises OnBeforePopup, the historical crash site.
  await sendClick(video.x + video.displayWidth * 0.34, video.y + video.displayHeight * 0.20)
  const startedAt = Date.now()
  while (Date.now() - startedAt < OBSERVE_MS) {
    await sleep(5_000)
    const state = await evaluate(`(() => {
      const element = document.querySelector('video')
      return {
        readyState: element?.readyState || 0,
        frames: element?.getVideoPlaybackQuality?.().totalVideoFrames || 0,
        connection: window.__conn?.status?.() || '',
      }
    })()`).catch(() => null)
    if (!state || state.readyState < 2 || state.connection === 'closed' || state.connection === 'failed') {
      throw new Error(`WebView connection lost: ${JSON.stringify(state)}`)
    }
  }
  const finalState = await evaluate(`(() => {
    const element = document.querySelector('video')
    return {
      readyState: element?.readyState || 0,
      frames: element?.getVideoPlaybackQuality?.().totalVideoFrames || 0,
      connection: window.__conn?.status?.() || '',
    }
  })()`)
  console.log(`PASS WebView input and ${OBSERVE_MS}ms stability ${JSON.stringify(finalState)}`)
}

main()
  .catch((error) => { console.error(`FAIL ${error.message}`); process.exitCode = 1 })
  .finally(async () => {
    if (ws?.readyState === WebSocket.OPEN && launchedInstanceId && launchedCsrf) {
      try {
        // The WebClient runs on the remote Render origin. Re-navigating the CDP
        // target back to Console can leave Page.navigate pending while WebRTC
        // unloads. Reuse the browser's guest cookie from Node instead.
        const stored = await command('Storage.getCookies')
        const consoleHost = new URL(consoleBase).hostname
        const cookie = (stored.cookies || [])
          .filter((item) => item.domain.replace(/^\./, '') === consoleHost)
          .map((item) => `${item.name}=${item.value}`)
          .join('; ')
        const response = await fetch(
          `${consoleBase}/api/v1/${launchedAsUser ? 'user' : 'public'}/instances/${encodeURIComponent(launchedInstanceId)}/stop`,
          {
            method: 'POST',
            signal: AbortSignal.timeout(5000),
            headers: {
              Cookie: cookie,
              Origin: new URL(consoleBase).origin,
              Referer: `${new URL(consoleBase).origin}/`,
              'Content-Type': 'application/json',
              'X-CSRF-Token': launchedCsrf,
            },
            body: JSON.stringify({ reason: 'automated WebView stability test complete' }),
          },
        )
        const stopped = { ok: response.ok, status: response.status }
        console.log(`CLEANUP instance=${launchedInstanceId} ${JSON.stringify(stopped)}`)
      } catch { /* the instance may already have crashed and been reaped */ }
    }
    try { ws?.close() } catch { /* ignore */ }
    try { chrome.kill() } catch { /* ignore */ }
    // CDP WebSocket/Chrome subprocess handles can linger after a failed remote
    // cleanup. Acceptance runs must always terminate with the recorded result.
    setTimeout(() => process.exit(process.exitCode || 0), 1_000)
  })
