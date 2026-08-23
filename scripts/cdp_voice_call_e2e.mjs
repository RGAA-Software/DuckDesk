// Real-browser WebClient voice-call acceptance probe.
// The caller must arrange the real px_panel decision while this script waits:
//   WEB_URL='http://host:port/web_client/?...' EXPECTED_DECISION=accept \
//     node scripts/cdp_voice_call_e2e.mjs
// No credentials are embedded; the evidence file redacts sensitive query keys.
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const WEB_URL = process.env.WEB_URL
const EXPECTED_DECISION = (process.env.EXPECTED_DECISION || 'accept').toLowerCase()
const CHROME = process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const OUT_DIR = path.resolve(process.env.OUT_DIR || 'tests/artifacts/voice_call_e2e')
const CDP_PORT = Number(process.env.CDP_PORT || 9460)
const TIMEOUT_MS = Number(process.env.VOICE_TIMEOUT_MS || 50_000)
const ALLOW_INSECURE_MEDIA_TEST = process.env.ALLOW_INSECURE_MEDIA_TEST === '1'
const EXPECT_INSECURE_BLOCK = process.env.EXPECT_INSECURE_BLOCK === '1'
if (!WEB_URL) throw new Error('WEB_URL is required')
if (!['accept', 'reject', 'timeout'].includes(EXPECTED_DECISION)) {
  throw new Error('EXPECTED_DECISION must be accept, reject, or timeout')
}
fs.mkdirSync(OUT_DIR, { recursive: true })

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))
const profile = path.join(os.tmpdir(), `cdp-voice-${Date.now()}`)
const chromeArgs = [
  '--headless=new', `--remote-debugging-port=${CDP_PORT}`,
  `--user-data-dir=${profile}`, '--no-first-run',
  '--autoplay-policy=no-user-gesture-required', '--window-size=1600,900',
]
if (process.env.REAL_MEDIA !== '1') {
  chromeArgs.push('--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream')
}
if (ALLOW_INSECURE_MEDIA_TEST) {
  chromeArgs.push(`--unsafely-treat-insecure-origin-as-secure=${new URL(WEB_URL).origin}`)
}
chromeArgs.push('about:blank')
const chrome = spawn(CHROME, chromeArgs, { stdio: 'ignore' })

let ws
let messageId = 0
const pending = new Map()
const safeUrl = new URL(WEB_URL)
for (const key of ['c', 'password', 'pwd', 'pwd_md5', 'token', 'ticket', 'safety_pwd_md5']) {
  if (safeUrl.searchParams.has(key)) safeUrl.searchParams.set(key, '[redacted]')
}
const evidence = {
  caseIds: ['VC-WE-001', 'VC-WE-002', 'VC-WE-005', 'VC-WE-006', 'VC-WE-007', 'VC-WE-011', 'VC-WE-017', 'VC-SE-011'],
  url: safeUrl.toString(), expectedDecision: EXPECTED_DECISION,
  insecureMediaTestOverride: ALLOW_INSECURE_MEDIA_TEST,
  expectInsecureBlock: EXPECT_INSECURE_BLOCK,
  startedAt: new Date().toISOString(), checks: [], snapshots: {},
}

function check(name, ok, detail = undefined) {
  evidence.checks.push({ name, ok: Boolean(ok), detail })
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail === undefined ? '' : ` -- ${JSON.stringify(detail)}`}`)
  if (!ok) throw new Error(name)
}

function command(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++messageId
    pending.set(id, { resolve, reject })
    ws.send(JSON.stringify({ id, method, params }))
  })
}

async function evaluate(expression) {
  const result = await command('Runtime.evaluate', {
    expression, returnByValue: true, awaitPromise: true, userGesture: true,
  })
  if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails).slice(0, 600))
  return result.result?.value
}

async function waitDevtools() {
  for (let index = 0; index < 100; ++index) {
    try {
      const response = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)
      if (response.ok) return
    } catch { /* retry */ }
    await sleep(250)
  }
  throw new Error('Chrome DevTools endpoint not ready')
}

async function waitFor(label, predicate, timeoutMs = TIMEOUT_MS) {
  const deadline = Date.now() + timeoutMs
  let last
  while (Date.now() < deadline) {
    last = await voiceState().catch((error) => ({ error: String(error) }))
    if (predicate(last)) return last
    await sleep(250)
  }
  throw new Error(`${label} timed out; last=${JSON.stringify(last)}`)
}

async function voiceState() {
  return evaluate(`(() => ({
    connection: window.__conn?.status?.() ?? 'unknown',
    secureContext: window.isSecureContext,
    mediaDevicesAvailable: typeof navigator.mediaDevices?.getUserMedia === 'function',
    ...(window.__mic?.state?.() ?? {}),
  }))()`)
}

async function click(selector) {
  const point = await evaluate(`(() => {
    const element = document.querySelector(${JSON.stringify(selector)})
    if (!(element instanceof HTMLElement)) return { ok: false, reason: 'not found' }
    if (element instanceof HTMLButtonElement && element.disabled) return { ok: false, reason: 'disabled' }
    const rect = element.getBoundingClientRect()
    const style = getComputedStyle(element)
    if (rect.width <= 0 || rect.height <= 0 || style.visibility === 'hidden' || style.display === 'none') {
      return { ok: false, reason: 'not visible' }
    }
    return { ok: true, x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 }
  })()`)
  if (!point?.ok) throw new Error(`cannot click ${selector}: ${point?.reason}`)
  await command('Input.dispatchMouseEvent', {
    type: 'mousePressed', x: point.x, y: point.y, button: 'left', clickCount: 1,
  })
  await command('Input.dispatchMouseEvent', {
    type: 'mouseReleased', x: point.x, y: point.y, button: 'left', clickCount: 1,
  })
}

async function ensureControl(selector) {
  for (let index = 0; index < 20; ++index) {
    if (await evaluate(`(() => {
      const element = document.querySelector(${JSON.stringify(selector)})
      if (!(element instanceof HTMLElement)) return false
      const rect = element.getBoundingClientRect()
      const style = getComputedStyle(element)
      return rect.width > 0 && rect.height > 0 && style.visibility !== 'hidden' && style.display !== 'none'
    })()`)) return
    // Closing an Element Plus modal can briefly swallow the first click while
    // its overlay animates out. Retry the real floating-ball click instead of
    // assuming a single event always opens the menu.
    if (index % 4 === 0) await click('.float-ball')
    await sleep(100)
  }
  throw new Error(`floating control not visible: ${selector}`)
}

async function ensureVoiceAudioControl(selector) {
  for (let index = 0; index < 4; ++index) {
    try {
      await ensureControl(selector)
      return
    } catch { /* open the parent display menu and retry */ }
    await ensureControl('[data-testid="display-submenu-toggle"]')
    await click('[data-testid="display-submenu-toggle"]')
    await sleep(150)
  }
  throw new Error(`voice audio control not visible: ${selector}`)
}

async function audioStats() {
  return evaluate(`(async () => {
    const result = {
      outboundBytes: 0, inboundBytes: 0, outboundPackets: 0, inboundPackets: 0,
      voiceInboundBytes: 0, voiceInboundPackets: 0, receivers: [],
    }
    const stats = await window.__pc.getStats()
    stats.forEach((item) => {
      if (item.kind !== 'audio' && item.mediaType !== 'audio') return
      if (item.type === 'outbound-rtp') {
        result.outboundBytes += item.bytesSent ?? 0
        result.outboundPackets += item.packetsSent ?? 0
      } else if (item.type === 'inbound-rtp') {
        result.inboundBytes += item.bytesReceived ?? 0
        result.inboundPackets += item.packetsReceived ?? 0
      }
    })
    const audioTransceivers = window.__pc.getTransceivers()
      .filter((item) => item.receiver?.track?.kind === 'audio')
    const voiceReceiver = audioTransceivers[1]?.receiver
    for (const receiver of window.__pc.getReceivers().filter((item) => item.track?.kind === 'audio')) {
      let bytes = 0
      let packets = 0
      const receiverStats = await receiver.getStats()
      receiverStats.forEach((item) => {
        if (item.type === 'inbound-rtp' && (item.kind === 'audio' || item.mediaType === 'audio')) {
          bytes += item.bytesReceived ?? 0
          packets += item.packetsReceived ?? 0
        }
      })
      // The browser assigns remote track IDs, so identify the dedicated call
      // receiver by the second negotiated audio m-line/transceiver instead.
      const voice = receiver === voiceReceiver
      result.receivers.push({ trackId: receiver.track.id, bytes, packets, voice })
      if (voice) {
        result.voiceInboundBytes += bytes
        result.voiceInboundPackets += packets
      }
    }
    return result
  })()`)
}

async function screenshot(name) {
  // The controlled desktop can contain one-time credentials or customer data.
  // Cover every rendered video/canvas surface before recording UI evidence.
  await evaluate(`(() => {
    document.getElementById('__voice_e2e_redaction')?.remove()
    const layer = document.createElement('div')
    layer.id = '__voice_e2e_redaction'
    layer.style.cssText = 'position:fixed;inset:0;pointer-events:none;z-index:2147483647'
    for (const surface of document.querySelectorAll('video,canvas')) {
      const rect = surface.getBoundingClientRect()
      if (rect.width <= 0 || rect.height <= 0) continue
      const cover = document.createElement('div')
      cover.style.cssText = [
        'position:fixed', 'background:#202124',
        'display:flex', 'align-items:center', 'justify-content:center',
        'color:#fff', 'font:16px sans-serif',
        'left:' + rect.left + 'px', 'top:' + rect.top + 'px',
        'width:' + rect.width + 'px', 'height:' + rect.height + 'px',
      ].join(';')
      cover.textContent = 'Remote video redacted from test evidence'
      layer.appendChild(cover)
    }
    document.body.appendChild(layer)
  })()`)
  try {
    const result = await command('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false })
    const file = path.join(OUT_DIR, `${name}.png`)
    fs.writeFileSync(file, Buffer.from(result.data, 'base64'))
    return file
  } finally {
    await evaluate(`document.getElementById('__voice_e2e_redaction')?.remove()`)
  }
}

async function main() {
  await waitDevtools()
  const response = await fetch(
    `http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(WEB_URL)}`,
    { method: 'PUT' },
  )
  if (!response.ok) throw new Error(`opening page failed: HTTP ${response.status}`)
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

  if (EXPECT_INSECURE_BLOCK) {
    const blocked = await waitFor('connected insecure-context voice gate', (state) =>
      state.connection === 'connected' && state.supported === false &&
      state.secureContext === false && state.mediaDevicesAvailable === false &&
      /HTTPS|localhost/i.test(state.reason ?? ''))
    evidence.snapshots.insecureBlocked = blocked
    check('HTTP private-network page clearly blocks unavailable browser microphone',
      /HTTPS|localhost/i.test(blocked.reason ?? ''), blocked)
    check('insecure-context gate never captures or attaches a microphone',
      blocked.senderTrack === 'none' && blocked.localTracks.length === 0, blocked)
    return
  }

  const idle = await waitFor('WebRTC connection and capability', (state) =>
    state.connection === 'connected' && state.supported === true && state.phase === 'idle')
  evidence.snapshots.idle = idle
  check('capability enables voice only on connected authorized session', true, idle)
  check('microphone is untouched before user action',
    idle.senderTrack === 'none' && idle.localTracks.length === 0, idle)

  const sdp = await evaluate(`window.__pc.localDescription?.sdp ?? ''`)
  const audioSections = sdp.split(/(?=m=audio)/).filter((part) => part.startsWith('m=audio'))
  check('SDP has independent system and call audio m-lines', audioSections.length >= 2,
    { audioMlines: audioSections.length })
  check('second audio m-line is bidirectional for the call', /a=sendrecv/.test(audioSections[1]),
    audioSections[1]?.split('\n').slice(0, 8))

  await ensureControl('[data-testid="voice-call-toggle"]')
  await click('[data-testid="voice-call-toggle"]')
  let preflightObserved = false
  for (let index = 0; index < 20; ++index) {
    const confirmVisible = await evaluate(`document.querySelector('.el-message-box__btns .el-button--primary') instanceof HTMLElement`)
    if (confirmVisible) {
      preflightObserved = true
      const warning = await evaluate(`document.querySelector('.el-message-box__message')?.textContent?.trim() ?? ''`)
      check('preflight warns about controlled-computer system audio',
        /系统声音|系統聲音|System audio/.test(warning) &&
          /暂停|暫停|pause/i.test(warning), warning)
      await click('.el-message-box__btns .el-button:not(.el-button--primary)')
      const cancelled = await waitFor('preflight cancel cleanup', (state) =>
        state.phase === 'idle' && state.senderTrack === 'none' && state.localTracks.length === 0,
        3_000)
      check('cancelling preflight does not capture or send microphone audio', true, cancelled)

      await ensureControl('[data-testid="voice-call-toggle"]')
      await click('[data-testid="voice-call-toggle"]')
      let secondPreflight = false
      for (let retry = 0; retry < 20; ++retry) {
        secondPreflight = await evaluate(`document.querySelector('.el-message-box__btns .el-button--primary') instanceof HTMLElement`)
        if (secondPreflight) break
        await sleep(100)
      }
      check('preflight is shown again after cancellation', secondPreflight)
      await click('.el-message-box__btns .el-button--primary')
      break
    }
    await sleep(100)
  }
  check('headset-required capability always presents the preflight',
    idle.requiresHeadset !== true || preflightObserved,
    { requiresHeadset: idle.requiresHeadset, preflightObserved })
  const pendingState = await waitFor('outgoing authorization state', (state) =>
    state.phase === 'outgoing' && state.localTracks.length === 1)
  evidence.snapshots.pending = pendingState
  check('pending call keeps sender detached until Panel accepts',
    pendingState.senderTrack === 'none' && pendingState.localTracks.length === 1 &&
      pendingState.localTracks[0].readyState === 'live', pendingState)
  await screenshot('voice_pending')

  if (EXPECTED_DECISION !== 'accept') {
    const ended = await waitFor(`${EXPECTED_DECISION} cleanup`, (state) =>
      state.phase === 'error' && state.senderTrack === 'none' &&
      state.localTracks.length === 0 && state.lastMicTrackStopState.includes('ended') &&
      (EXPECTED_DECISION === 'timeout'
        ? state.reason === 'timeout'
        : Boolean(state.reason) && state.reason !== 'timeout'),
      EXPECTED_DECISION === 'timeout' ? 40_000 : TIMEOUT_MS)
    evidence.snapshots.ended = ended
    check(`${EXPECTED_DECISION} stops and detaches microphone`, true, ended)
    return
  }

  const connected = await waitFor('Panel accept and media attach', (state) =>
    state.phase === 'connected' && state.senderTrack === 'live' && state.localTracks.length === 1)
  evidence.snapshots.connected = connected
  check('accepted call attaches exactly one live microphone track', true, connected)
  check('call output has a separate live audio track',
    connected.voiceOutputTracks.some((track) => track.kind === 'audio' && track.readyState === 'live'),
    connected.voiceOutputTracks)

  const before = await audioStats()
  await sleep(4000)
  const after = await audioStats()
  evidence.snapshots.rtpBefore = before
  evidence.snapshots.rtpAfter = after
  check('browser microphone RTP advances', after.outboundBytes > before.outboundBytes, { before, after })
  check('dedicated Render call-audio RTP advances',
    after.voiceInboundBytes > before.voiceInboundBytes &&
      after.voiceInboundPackets > before.voiceInboundPackets, { before, after })

  await ensureVoiceAudioControl('[data-testid="voice-microphone-mute"]')
  await click('[data-testid="voice-microphone-mute"]')
  let mutedState = await waitFor('microphone mute state', (state) => state.micMuted === true, 3_000)
  check('microphone mute disables call track without ending it',
    mutedState.micMuted && mutedState.localTracks[0]?.enabled === false &&
      mutedState.localTracks[0]?.readyState === 'live', mutedState)
  await ensureVoiceAudioControl('[data-testid="voice-microphone-mute"]')
  await click('[data-testid="voice-microphone-mute"]')
  mutedState = await waitFor('microphone unmute state', (state) => state.micMuted === false, 3_000)
  check('microphone unmute restores call track',
    !mutedState.micMuted && mutedState.localTracks[0]?.enabled === true, mutedState)

  const originalSystemMute = mutedState.systemOutputMuted
  await ensureVoiceAudioControl('[data-testid="voice-speaker-mute"]')
  await click('[data-testid="voice-speaker-mute"]')
  let speakerState = await waitFor('call speaker mute state', (state) => state.speakerMuted === true, 3_000)
  check('call speaker mute does not mute system audio',
    speakerState.speakerMuted && speakerState.voiceOutputMuted === true &&
      speakerState.systemOutputMuted === originalSystemMute, speakerState)
  await ensureVoiceAudioControl('[data-testid="voice-speaker-mute"]')
  await click('[data-testid="voice-speaker-mute"]')
  speakerState = await waitFor('call speaker unmute state', (state) => state.speakerMuted === false, 3_000)
  check('call speaker unmute restores only call output',
    !speakerState.speakerMuted && speakerState.voiceOutputMuted === false &&
      speakerState.systemOutputMuted === originalSystemMute, speakerState)

  await screenshot('voice_connected')
  await ensureControl('[data-testid="voice-call-toggle"]')
  await click('[data-testid="voice-call-toggle"]')
  const idleAfterHangup = await waitFor('local hangup cleanup', (state) =>
    state.phase === 'idle' && state.senderTrack === 'none' && state.localTracks.length === 0 &&
      state.lastMicTrackStopState.includes('ended'))
  evidence.snapshots.hangup = idleAfterHangup
  check('hangup stops local track and returns to idle', true, idleAfterHangup)

  if (idleAfterHangup.requiresHeadset === true) {
    await ensureControl('[data-testid="voice-call-toggle"]')
    await click('[data-testid="voice-call-toggle"]')
    let repeatedPreflight = false
    for (let index = 0; index < 20; ++index) {
      repeatedPreflight = await evaluate(`document.querySelector('.el-message-box__btns .el-button--primary') instanceof HTMLElement`)
      if (repeatedPreflight) break
      await sleep(100)
    }
    check('a later call requires the safety preflight again', repeatedPreflight)
    await click('.el-message-box__btns .el-button:not(.el-button--primary)')
    const repeatedCancel = await waitFor('later preflight cancel cleanup', (state) =>
      state.phase === 'idle' && state.senderTrack === 'none' && state.localTracks.length === 0,
      3_000)
    check('later preflight cancellation still leaves microphone untouched', true, repeatedCancel)
  }
}

let failed
try {
  await main()
} catch (error) {
  failed = error
  evidence.error = String(error?.stack || error)
  console.error(evidence.error)
} finally {
  evidence.finishedAt = new Date().toISOString()
  evidence.result = failed ? 'FAIL' : 'PASS'
  try { evidence.finalScreenshot = await screenshot('voice_final') } catch { /* page may be gone */ }
  fs.writeFileSync(path.join(OUT_DIR, 'result.json'), JSON.stringify(evidence, null, 2))
  try { ws?.close() } catch { /* ignore */ }
  try { chrome.kill() } catch { /* ignore */ }
}
if (failed) process.exitCode = 1
