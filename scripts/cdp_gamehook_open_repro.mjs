// Reproduces the CMS “打开” operation in an isolated browser process and
// reports whether the selected game-hook instance remains running.
import { spawn } from 'node:child_process'
import { readFile } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'

const cms = process.env.CMS_URL || 'https://127.0.0.1:30500'
const nodeId = process.env.CMS_NODE_ID || 'node-10-64be35f1'
const cdpPort = Number(process.env.CDP_PORT || 9500)
const keepClientMs = Number(process.env.CMS_KEEP_CLIENT_MS || 0)
// Test machine uses the CMS' development/self-signed certificate.
process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0'
const chrome = spawn('C:/Program Files/Google/Chrome/Application/chrome.exe', [
  '--headless=new', '--ignore-certificate-errors', '--disable-gpu',
  `--remote-debugging-port=${cdpPort}`,
  `--user-data-dir=${path.join(os.tmpdir(), `px-gamehook-open-${Date.now()}`)}`,
  '--no-first-run', 'about:blank',
], { stdio: 'ignore' })

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))
async function inspectFlv(url) {
  const response = await fetch(url)
  const reader = response.body?.getReader()
  if (!reader) return { http: response.status, bodyMissing: true }
  const chunks = []
  let size = 0
  while (size < 250_000) {
    const { done, value } = await reader.read()
    if (done) break
    chunks.push(value)
    size += value.length
  }
  await reader.cancel()
  const data = Buffer.concat(chunks)
  const summary = { http: response.status, bytes: data.length, validHeader: data.subarray(0, 3).toString() === 'FLV', videoTimestamps: [], audioTimestamps: [], videoPackets: [] }
  let offset = 13
  while (summary.validHeader && offset + 11 <= data.length && summary.videoTimestamps.length < 8) {
    const type = data[offset]
    const dataSize = data.readUIntBE(offset + 1, 3)
    const timestamp = data.readUIntBE(offset + 4, 3) | (data[offset + 7] << 24)
    const payload = offset + 11
    if (payload + dataSize + 4 > data.length) break
    if (type === 9) {
      summary.videoTimestamps.push(timestamp)
      summary.videoPackets.push({ timestamp, frameType: data[payload] >> 4, packetType: data[payload + 1] })
    } else if (type === 8 && summary.audioTimestamps.length < 8) {
      summary.audioTimestamps.push(timestamp)
    }
    offset = payload + dataSize + 4
  }
  return summary
}
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

try {
  for (let retry = 0; retry < 60; retry += 1) {
    try { if ((await fetch(`http://127.0.0.1:${cdpPort}/json/version`)).ok) break } catch {}
    await sleep(250)
  }
  const target = await (await fetch(`http://127.0.0.1:${cdpPort}/json/new?${encodeURIComponent(cms + '/')}`, { method: 'PUT' })).json()
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
  await command('Page.navigate', { url: cms + '/' })
  await sleep(600)
  const running = await evaluate(`(async () => {
    const list = await (await fetch(${JSON.stringify(cms + '/api/v1/app/control/app/instance/list')})).json()
    return (list.data || []).find((item) => item.node_id === ${JSON.stringify(nodeId)} && item.state === 'running') || null
  })()`)
  if (!running) throw new Error('no running instance for node')
  const clientUrl = `http://127.0.0.1:${running.listen_port}/web_client/?deviceId=${encodeURIComponent(running.device_id)}&instanceId=${encodeURIComponent(running.instance_id)}`
  await command('Page.navigate', { url: clientUrl })
  const queryInstance = async () => {
    const list = await (await fetch(cms + '/api/v1/app/control/app/instance/list')).json()
    const item = (list.data || []).find((value) => value.instance_id === running.instance_id)
    return item ? { state: item.state, pid: item.pid, hasError: Boolean(item.error) } : null
  }
  const queryLive = async () => {
    const status = await (await fetch(cms + '/api/v1/live/control/status?device_id=' + encodeURIComponent(running.device_id) + '&app_id=' + encodeURIComponent(running.app_id))).json()
    return { online: status.data?.online === true, message: status.data?.message || '' }
  }
  const queryClient = () => evaluate(`({
    connection: window.__conn?.status?.() ?? 'unavailable',
    reconnects: window.__conn?.reconnectCount?.() ?? -1,
    title: document.title
  })`)
  const queryCmsFlv = async () => {
    try {
      const status = await fetch(cms + '/api/v1/live/control/status?device_id=' + encodeURIComponent(running.device_id) + '&app_id=' + encodeURIComponent(running.app_id)).then((response) => response.json())
      return await inspectFlv(new URL(status.data.play_url, cms))
    } catch (error) {
      return { error: error instanceof Error ? error.name : 'request-failed' }
    }
  }
  const queryZlmTracks = async () => {
    const config = await readFile('output/px_cms/config.ini', 'utf8')
    const section = config.match(/\[api\][\s\S]*?(?=\n\[|$)/i)?.[0] || ''
    const secret = section.match(/^secret\s*=\s*(.+)$/mi)?.[1]?.trim() || ''
    const stream = `${running.device_id}__app__${running.app_id}`
    const api = new URL('http://127.0.0.1:12888/index/api/getMediaList')
    api.searchParams.set('secret', secret)
    api.searchParams.set('app', 'live')
    api.searchParams.set('stream', stream)
    const result = await (await fetch(api)).json()
    return (result.data || []).map((media) => ({
      schema: media.schema,
      readers: media.readerCount,
      tracks: (media.tracks || []).map((track) => ({ codec: track.codec, fps: track.fps, width: track.width, height: track.height, ready: track.ready })),
    }))
  }
  await sleep(3_000)
  const after3s = await queryInstance()
  const liveAfter3s = await queryLive()
  const clientAfter3s = await queryClient()
  await sleep(12_000)
  const cmsFlvAfter15s = await queryCmsFlv()
  const zlmTracksAfter15s = await queryZlmTracks()
  const after15s = await queryInstance()
  const liveAfter15s = await queryLive()
  const clientAfter15s = await queryClient()
  console.log(JSON.stringify({ before: 'running', after3s, liveAfter3s, clientAfter3s, cmsFlvAfter15s, zlmTracksAfter15s, after15s, liveAfter15s, clientAfter15s, clientOpened: true }))
  if (keepClientMs > 0) await sleep(keepClientMs)
} finally {
  try { ws?.close() } catch {}
  try { chrome.kill() } catch {}
}
