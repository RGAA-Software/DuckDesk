// Compare sustained byte delivery from ZLMediaKit and the CMS HTTPS relay.
// Local credentials are used only to obtain a short-lived play ticket and
// are never printed.
import { createHash } from 'node:crypto'

process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0'

const cmsBase = process.env.CMS_URL || 'https://127.0.0.1:30500'
const mediaBase = process.env.MEDIA_URL || 'http://127.0.0.1:12888'
const deviceId = process.env.CMS_DEVICE_ID || '074723054'
const appId = process.env.CMS_APP_ID || 'app-9-01126a41'
const observeMs = Number(process.env.CMS_STREAM_PROBE_MS || 20000)

const auth = await fetch(`${cmsBase}/api/v1/auth/control/get/auth/status`).then((response) => response.json())
const username = auth.data?.username
const password = auth.data?.password
if (!username || !password) throw new Error('local CMS credentials unavailable')

const login = await fetch(`${cmsBase}/api/v1/auth/control/verify/auth/account`, {
  method: 'POST',
  headers: { 'content-type': 'application/json' },
  body: JSON.stringify({
    username,
    password: createHash('md5').update(password).digest('hex'),
  }),
}).then((response) => response.json())
if (login.code !== 200 || !login.data) throw new Error('CMS login failed')

const status = await fetch(
  `${cmsBase}/api/v1/live/control/status?device_id=${encodeURIComponent(deviceId)}`
    + `&app_id=${encodeURIComponent(appId)}&appkey=${encodeURIComponent(login.data)}`,
).then((response) => response.json())
if (!status.data?.online || !status.data?.play_url) {
  throw new Error(`stream unavailable: ${status.data?.message || status.message || 'unknown'}`)
}

const streamId = status.data.stream_id
const urls = {
  direct: `${mediaBase}/live/${encodeURIComponent(streamId)}.live.flv`,
  cms: new URL(status.data.play_url, cmsBase).toString(),
}

async function measure(name, url) {
  const controller = new AbortController()
  const deadline = setTimeout(() => controller.abort(), observeMs)
  const samples = []
  let bytes = 0
  let reads = 0
  let lastDataAt = Date.now()
  let response
  const startedAt = Date.now()
  const sampler = setInterval(() => {
    samples.push({
      second: Number(((Date.now() - startedAt) / 1000).toFixed(1)),
      bytes,
      reads,
      idleMs: Date.now() - lastDataAt,
    })
  }, 1000)
  try {
    response = await fetch(url, { signal: controller.signal, cache: 'no-store' })
    const reader = response.body?.getReader()
    if (!reader) throw new Error('response body unavailable')
    while (true) {
      const { done, value } = await reader.read()
      if (done) break
      bytes += value.byteLength
      reads += 1
      lastDataAt = Date.now()
    }
  } catch (error) {
    if (error?.name !== 'AbortError') throw error
  } finally {
    clearInterval(deadline)
    clearInterval(sampler)
  }
  return {
    name,
    http: response?.status,
    protocolContentType: response?.headers.get('content-type'),
    transferEncoding: response?.headers.get('transfer-encoding'),
    contentLength: response?.headers.get('content-length'),
    totalBytes: bytes,
    totalReads: reads,
    samples,
  }
}

const results = await Promise.all(Object.entries(urls).map(([name, url]) => measure(name, url)))
console.log(JSON.stringify({ observeMs, streamId, results }))
