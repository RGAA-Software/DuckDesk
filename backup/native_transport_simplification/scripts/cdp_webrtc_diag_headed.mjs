// WebRTC 有头诊断:真实浏览器窗口(非 headless),重点采集
// decoderImplementation / droppedVideoFrames / jitterBuffer 目标延迟
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'

const CHROME = process.env.CHROME_PATH
  || 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const PAGE_URL =
  process.env.WEB_URL ||
  `http://127.0.0.1:${process.env.RENDER_PORT || '32000'}/web_client/?deviceId=${encodeURIComponent(process.env.DEVICE_ID || 'debug1')}`
const CDP_PORT = Number(process.env.CDP_PORT || 9225)
const SAMPLE_SECONDS = Number(process.env.SAMPLE_SECONDS || 30)

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const profile = path.join(os.tmpdir(), `cdp-headed-${Date.now()}`)
const chrome = spawn(CHROME, [
  `--remote-debugging-port=${CDP_PORT}`,
  `--user-data-dir=${profile}`,
  '--no-first-run',
  '--new-window',
  '--window-position=2200,100',
  '--window-size=1600,950',
  'about:blank',
], { stdio: 'ignore' })
process.on('exit', () => { try { chrome.kill() } catch { /* ignore */ } })

let msgId = 0
const pending = new Map()
let ws
function cmd(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++msgId
    pending.set(id, { resolve, reject })
    ws.send(JSON.stringify({ id, method, params }))
  })
}
async function evaluate(expression) {
  const r = await cmd('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true, userGesture: true })
  if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 300))
  return r.result?.value
}
async function waitDevtools() {
  for (let i = 0; i < 60; i++) {
    try { const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`); if (r.ok) return } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error('devtools 未就绪')
}

const STATS_JS = `(async () => {
  const pc = window.__pc
  if (!pc) return { err: 'no __pc' }
  const stats = await pc.getStats()
  const out = { conn: pc.connectionState }
  let codecId = ''
  stats.forEach((s) => {
    if (s.type === 'inbound-rtp' && s.kind === 'video') {
      codecId = s.codecId
      out.inbound = {
        fps: s.framesPerSecond, recv: s.framesReceived, dec: s.framesDecoded,
        lost: s.packetsLost, pkts: s.packetsReceived,
        dropped: s.framesDropped,
        jbDelay: s.jitterBufferDelay,
        jbTarget: s.jitterBufferTargetDelay,
        jbMin: s.jitterBufferMinimumDelay,
        jbEmitted: s.jitterBufferEmittedCount,
        keyDec: s.keyFramesDecoded,
        freezeCount: s.freezeCount, totalFreezesDuration: s.totalFreezesDuration,
        bytes: s.bytesReceived,
        decoder: s.decoderImplementation,
        // 解码耗时(若暴露):每帧平均解码 ms = totalDecodeTime/framesDecoded
        totalDecodeTime: s.totalDecodeTime,
        totalProcessingDelay: s.totalProcessingDelay,
      }
    }
    if (s.type === 'candidate-pair' && s.state === 'succeeded' && s.nominated) {
      out.pair = { rtt: s.currentRoundTripTime, inBwe: s.availableIncomingBitrate }
    }
  })
  if (codecId) {
    stats.forEach((s) => {
      if (s.type === 'codec' && s.id === codecId) {
        out.codec = { mime: s.mimeType, pt: s.payloadType, sdpFmtpLine: s.sdpFmtpLine }
      }
    })
  }
  const v = document.querySelector('video')
  if (v) {
    const q = v.getVideoPlaybackQuality()
    out.playback = {
      total: q.totalVideoFrames, dropped: q.droppedVideoFrames,
      w: v.videoWidth, h: v.videoHeight,
      raf: v.requestVideoFrameCallback ? 'yes' : 'no',
    }
  }
  return out
})()`

async function main() {
  await waitDevtools()
  const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(PAGE_URL)}`, { method: 'PUT' })
  const target = await r.json()
  ws = new WebSocket(target.webSocketDebuggerUrl)
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data)
    if (m.id && pending.has(m.id)) {
      const p = pending.get(m.id)
      pending.delete(m.id)
      m.error ? p.reject(new Error(m.error.message)) : p.resolve(m.result)
    }
  }
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej })
  await cmd('Runtime.enable')

  let connected = false
  for (let i = 0; i < 60; i++) {
    const st = await evaluate(`window.__pc ? window.__pc.connectionState : 'none'`).catch(() => 'err')
    if (st === 'connected') { connected = true; break }
    await sleep(500)
  }
  console.log('connected:', connected)
  if (!connected) throw new Error('未连接')

  if (process.env.STOP_AUDIO === '1') {
    const r = await evaluate(`(() => {
      const pc = window.__pc
      const ar = pc.getReceivers().find((x) => x.track && x.track.kind === 'audio')
      if (ar) { ar.track.stop(); return 'audio stopped' }
      return 'no audio receiver'
    })()`)
    console.log('STOP_AUDIO:', r)
  }

  let prev = null
  for (let t = 0; t < SAMPLE_SECONDS; t += 3) {
    await sleep(3000)
    const s = await evaluate(STATS_JS).catch((e) => ({ err: e.message }))
    if (s.inbound && prev?.inbound) {
      const dt = 3
      s.calc = {
        kbps: Math.round(((s.inbound.bytes - prev.inbound.bytes) * 8) / dt / 1000),
        recvFps: Math.round(((s.inbound.recv - prev.inbound.recv) / dt) * 10) / 10,
        decFps: Math.round(((s.inbound.dec - prev.inbound.dec) / dt) * 10) / 10,
        dispFps: s.playback && prev.playback
          ? Math.round(((s.playback.total - prev.playback.total) / dt) * 10) / 10
          : null,
        dispDropped: s.playback && prev.playback ? s.playback.dropped - prev.playback.dropped : null,
        decMsPerFrame:
          s.inbound.totalDecodeTime != null && prev.inbound.totalDecodeTime != null && s.inbound.dec > prev.inbound.dec
            ? Math.round(((s.inbound.totalDecodeTime - prev.inbound.totalDecodeTime) / (s.inbound.dec - prev.inbound.dec)) * 10000) / 10
            : null,
        jbDelayPerFrameMs:
          s.inbound.jbDelay != null && s.inbound.dec > prev.inbound.dec
            ? Math.round(((s.inbound.jbDelay - prev.inbound.jbDelay) / (s.inbound.dec - prev.inbound.dec)) * 100000) / 100
            : null,
      }
    }
    console.log(`[t=${t + 3}s]`, JSON.stringify(s))
    prev = s
  }
}

main()
  .catch((e) => { console.error('异常:', e.message); process.exitCode = 1 })
  .finally(() => {
    try { ws?.close() } catch { /* ignore */ }
    try { chrome.kill() } catch { /* ignore */ }
    setTimeout(() => process.exit(process.exitCode ?? 0), 500).unref()
  })
