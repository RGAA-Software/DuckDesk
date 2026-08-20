// Browser-level smoke test for the CMS HTTP-FLV live viewer.  It signs in
// using the locally exposed authorization credentials without logging them,
// then verifies the real <video> element receives decoded frames.
import { createHash } from 'node:crypto'
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'

const base = process.env.CMS_URL || 'https://127.0.0.1:30500'
const cdpPort = Number(process.env.CDP_PORT || 9511)
const observeMs = Number(process.env.CMS_PLAYBACK_OBSERVE_MS || 7000)
process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0'

const chrome = spawn('C:/Program Files/Google/Chrome/Application/chrome.exe', [
  '--headless=new', '--ignore-certificate-errors', '--disable-gpu',
  `--remote-debugging-port=${cdpPort}`,
  `--user-data-dir=${path.join(os.tmpdir(), `cms-live-viewer-${Date.now()}`)}`,
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
const evaluate = async (expression) => {
  const result = await command('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true })
  if (result.exceptionDetails) throw new Error(result.exceptionDetails.text || 'browser evaluation failed')
  return result.result?.value
}

async function inspectFlv(url) {
  const response = await fetch(url)
  const reader = response.body?.getReader()
  if (!reader) return { http: response.status, bodyMissing: true }
  const chunks = []
  let size = 0
  while (size < 1_000_000) {
    const { done, value } = await reader.read()
    if (done) break
    chunks.push(value)
    size += value.length
  }
  await reader.cancel()
  const data = Buffer.concat(chunks)
  const result = { http: response.status, bytes: data.length, videoTags: 0, keyFrames: 0, avcConfig: 0, avcNalus: 0, firstTimestamp: null, lastTimestamp: null, videoTimestampSample: [] }
  if (data.subarray(0, 3).toString() !== 'FLV') return { ...result, validHeader: false }
  let offset = 13
  while (offset + 11 <= data.length) {
    const type = data[offset]
    const dataSize = data.readUIntBE(offset + 1, 3)
    const timestamp = data.readUIntBE(offset + 4, 3) | (data[offset + 7] << 24)
    const payload = offset + 11
    if (payload + dataSize + 4 > data.length) break
    if (type === 9 && dataSize >= 2) {
      result.videoTags += 1
      result.firstTimestamp ??= timestamp
      result.lastTimestamp = timestamp
      if (result.videoTimestampSample.length < 10) result.videoTimestampSample.push(timestamp)
      if ((data[payload] >> 4) === 1) result.keyFrames += 1
      if ((data[payload] & 0x0f) === 7 && data[payload + 1] === 0) result.avcConfig += 1
      if ((data[payload] & 0x0f) === 7 && data[payload + 1] === 1) result.avcNalus += 1
    }
    offset = payload + dataSize + 4
  }
  return { ...result, validHeader: true }
}

try {
  for (let retry = 0; retry < 60; retry += 1) {
    try { if ((await fetch(`http://127.0.0.1:${cdpPort}/json/version`)).ok) break } catch {}
    await sleep(250)
  }
  const auth = await fetch(`${base}/api/v1/auth/control/get/auth/status`).then((response) => response.json())
  const username = auth.data?.username
  const password = auth.data?.password
  if (!username || !password) throw new Error('local CMS credentials unavailable')
  const login = await fetch(`${base}/api/v1/auth/control/verify/auth/account`, {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ username, password: createHash('md5').update(password).digest('hex') }),
  }).then((response) => response.json())
  const appkey = login.data
  if (login.code !== 200 || !appkey) throw new Error('CMS login failed')
  const status = await fetch(`${base}/api/v1/live/control/status?device_id=074723054&app_id=app-9-01126a41&appkey=${encodeURIComponent(appkey)}`).then((response) => response.json())
  const flv = await inspectFlv(new URL(status.data?.play_url, base))

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
  await sleep(800)
  await evaluate(`(() => {
    localStorage.setItem('appkey', ${JSON.stringify(appkey)})
    localStorage.setItem('token', 'browser-smoke-test')
  })()`)
  await command('Page.navigate', { url: base + '/live-viewer' })
  await sleep(3000)
  const entryState = await evaluate(`(() => {
    const video = document.querySelector('video')
    return {
      videoPresent: Boolean(video),
      currentTime: video?.currentTime || 0,
      title: document.title,
      text: document.body.innerText.slice(0, 400),
    }
  })()`)
  if (!entryState?.videoPresent) {
    throw new Error(`live stream did not load automatically on entry: ${entryState?.text || ''}`)
  }
  await evaluate(`(() => {
    window.__cmsPlaybackEvents = []
    const video = document.querySelector('video')
    for (const name of ['playing', 'waiting', 'stalled', 'error', 'pause', 'canplay']) {
      video?.addEventListener(name, () => window.__cmsPlaybackEvents.push({
        name,
        at: Number(video.currentTime.toFixed(3)),
        wall: Date.now(),
      }))
    }
  })()`)
  const samples = []
  const sampleCount = Math.max(1, Math.ceil(observeMs / 1000))
  for (let index = 0; index < sampleCount; index += 1) {
    await sleep(1000)
    samples.push(await evaluate(`(() => {
    const video = document.querySelector('video')
    const error = [...document.querySelectorAll('.ant-alert-error')]
      .map((item) => item.textContent?.trim()).filter(Boolean).join(' | ')
    const flvRequests = performance.getEntriesByType('resource')
      .filter((entry) => entry.name.includes('/live/control/play/')).length
    const buffered = video?.buffered
    const bufferedEnd = buffered?.length ? buffered.end(buffered.length - 1) : 0
    const bufferedStart = buffered?.length ? buffered.start(buffered.length - 1) : 0
    return video ? {
      readyState: video.readyState,
      currentTime: Number(video.currentTime.toFixed(3)),
      bufferedStart: Number(bufferedStart.toFixed(3)),
      bufferedEnd: Number(bufferedEnd.toFixed(3)),
      bufferedLatency: Number((bufferedEnd - video.currentTime).toFixed(3)),
      playbackRate: video.playbackRate,
      paused: video.paused,
      networkState: video.networkState,
      videoWidth: video.videoWidth,
      videoHeight: video.videoHeight,
      decodedFrames: video.getVideoPlaybackQuality?.().totalVideoFrames ?? 0,
      droppedFrames: video.getVideoPlaybackQuality?.().droppedVideoFrames ?? 0,
      flvRequests,
      error,
    } : { videoMissing: true, flvRequests, error }
  })()`))
  }
  const events = await evaluate('window.__cmsPlaybackEvents || []')
  const result = samples.at(-1)
  const movingSamples = samples.filter((sample, index) => index === 0 || sample.currentTime > samples[index - 1].currentTime + 0.2).length
  console.log(JSON.stringify({ ...result, flv, entryState, observeMs, movingSamples, events, samples }))
  process.exitCode = result?.videoWidth > 0 && result?.currentTime > 0 && movingSamples >= Math.max(1, sampleCount - 2) ? 0 : 1
} finally {
  try { ws?.close() } catch {}
  try { chrome.kill() } catch {}
}
