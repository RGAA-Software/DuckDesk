// CDP 无头 Chrome 验证:浏览器麦克风上行 -> render 解码 -> WASAPI 播放
// 用法: node scripts/cdp_mic_uplink_test.mjs
// 依赖: 无(Node 22 内置 fetch/WebSocket),Chrome + GoDesk 套件已在运行
// 无头 Chrome 用假音频设备(getUserMedia 无需真麦克风,输出固定蜂鸣音)
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
// 插件内 LOGI 写到插件自己的日志文件
const RENDER_LOG = 'C:/Users/Public/GoDesk/px_logs/plugin_net_rtc_local.dll.log'
const PAGE_URL =
  'http://127.0.0.1:20371/web_client/?deviceId=600378210&streamId=micup1&pwd_md5=698d51a19d8a121ce581499d7b701668'
const CDP_PORT = 9223

const results = []
function report(name, ok, detail = '') {
  results.push({ name, ok, detail })
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  -- ' + detail : ''}`)
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

// ---------- Chrome 启动(假麦克风)----------
const profile = path.join(os.tmpdir(), `cdp-mic-${Date.now()}`)
const chrome = spawn(
  CHROME,
  [
    '--headless=new',
    `--remote-debugging-port=${CDP_PORT}`,
    `--user-data-dir=${profile}`,
    '--no-first-run',
    '--autoplay-policy=no-user-gesture-required',
    '--use-fake-device-for-media-stream',
    '--use-fake-ui-for-media-stream',
    'about:blank',
  ],
  { stdio: 'ignore' },
)
process.on('exit', () => {
  try { chrome.kill() } catch { /* ignore */ }
})

async function waitDevtools() {
  for (let i = 0; i < 60; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)
      if (r.ok) return
    } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error('devtools 端口未就绪')
}

// ---------- CDP 会话 ----------
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
  if (r.exceptionDetails) throw new Error('evaluate 异常: ' + JSON.stringify(r.exceptionDetails).slice(0, 300))
  return r.result?.value
}

const CLICK_JS = (text, scope = '.float-toolbar') => `(() => {
  const els = [...document.querySelectorAll('${scope} button')]
  const el = els.find((e) => e.textContent.trim().includes(${JSON.stringify(text)}))
  if (!el) return 'NOT_FOUND'
  el.click()
  return 'OK'
})()`

function readNewLog(fromOffset) {
  const fd = fs.openSync(RENDER_LOG, 'r')
  try {
    const size = fs.fstatSync(fd).size
    if (size <= fromOffset) return ''
    const buf = Buffer.alloc(size - fromOffset)
    fs.readSync(fd, buf, 0, buf.length, fromOffset)
    return buf.toString('utf8')
  } finally {
    fs.closeSync(fd)
  }
}

async function getAudioOutboundBytes() {
  return evaluate(`(async () => {
    const stats = await window.__pc.getStats()
    let bytes = -1
    stats.forEach((s) => {
      if (s.type === 'outbound-rtp' && (s.kind === 'audio' || s.mediaType === 'audio')) {
        bytes = (bytes < 0 ? 0 : bytes) + s.bytesSent
      }
    })
    return bytes
  })()`)
}

async function main() {
  // 日志偏移在打开页面之前记录:OnAddTrack/first PCM 在建连时就会打印
  const off = fs.statSync(RENDER_LOG).size
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

  // 等待 WebRTC 连接建立
  let connected = false
  for (let i = 0; i < 80; i++) {
    const tag = await evaluate(`document.querySelector('.toolbar .el-tag')?.textContent?.trim() ?? ''`).catch(() => '')
    if (tag === '已连接') { connected = true; break }
    await sleep(500)
  }
  report('WebRTC 自动连接', connected)
  if (!connected) {
    const logs = await evaluate(`document.querySelector('.log-panel')?.textContent?.slice(-800) ?? ''`).catch(() => '')
    console.log('页面日志尾部:', logs)
    throw new Error('连接失败,中止')
  }

  // 1. offer 里应有 sendrecv 音频 m-line(麦克风占位 transceiver)
  const sdpInfo = await evaluate(`(() => {
    const sdp = window.__pc.localDescription.sdp
    const audioLines = sdp.split('m=').slice(1).map((s) => s.slice(0, 5).trim()).filter((s) => s.startsWith('audio'))
    const sendrecv = (sdp.match(/a=sendrecv/g) || []).length
    return { audioMlines: audioLines.length, sendrecv }
  })()`)
  report('offer 含 sendrecv 音频 m-line', sdpInfo.audioMlines >= 1 && sdpInfo.sendrecv >= 1, JSON.stringify(sdpInfo))

  // 2. 展开工具条,点「开启麦克风」
  await evaluate(`(() => { const b = document.querySelector('.float-toolbar .fab'); if (b) b.click(); return 'OK' })()`)
  await sleep(500)
  report('点击「开启麦克风」', (await evaluate(CLICK_JS('开启麦克风'))) === 'OK')
  await sleep(3000)
  const micOn = await evaluate(`window.__mic.on()`)
  report('麦克风状态开启', micOn === true)

  // 3. 浏览器侧确实在发音频包(outbound-rtp audio bytesSent 增长)
  const b1 = await getAudioOutboundBytes()
  await sleep(3000)
  const b2 = await getAudioOutboundBytes()
  report('浏览器音频上行 bytesSent 增长', b1 >= 0 && b2 > b1, `${b1} -> ${b2}`)

  // 4. render 侧插件日志:收到音频轨 + 解码后 PCM 到达 sink(播放由默认 ADM 自动外放)
  await sleep(6000) // 等 RemoteAudioSink 5s 统计日志
  const log = readNewLog(off)
  const lineOf = (kw) => log.split('\n').filter((l) => l.includes(kw)).slice(-2).join(' | ').trim()
  report('render OnAddTrack 收到远端音频轨', log.includes('OnAddTrack: remote audio track'), lineOf('OnAddTrack'))
  report('render sink 收到 PCM(解码正常)', log.includes('RemoteAudioSink first PCM'), lineOf('first PCM'))
  const statsLine = log.split('\n').filter((l) => l.includes('RemoteAudioSink stats')).slice(-1)[0] ?? ''
  const mRx = statsLine.match(/rx frames total=(\d+)/)
  report('render PCM 持续接收(rx 增长)', !!mRx && Number(mRx[1]) > 0, statsLine.trim())

  // 5. 关闭麦克风:replaceTrack(null),上行字节应停止增长
  report('点击「关闭麦克风」', (await evaluate(CLICK_JS('关闭麦克风'))) === 'OK')
  await sleep(2000)
  const micOff = await evaluate(`window.__mic.on()`)
  const b3 = await getAudioOutboundBytes()
  await sleep(3000)
  const b4 = await getAudioOutboundBytes()
  report('麦克风状态关闭', micOff === false)
  report('关闭后上行停止(bytesSent 基本不增)', b4 - b3 < b2 - b1, `${b3} -> ${b4} (之前 ${b1} -> ${b2})`)

  // 6. 连接仍然存活(render 未崩溃)
  const state = await evaluate(`window.__pc.connectionState`)
  report('连接仍存活', state === 'connected', `connectionState=${state}`)

  const failed = results.filter((x) => !x.ok)
  console.log(`\n==== ${results.length - failed.length}/${results.length} 项通过 ====`)
  if (failed.length) process.exitCode = 1
}

main()
  .catch((e) => { console.error('测试脚本异常:', e.message); process.exitCode = 1 })
  .finally(() => { try { chrome.kill() } catch { /* ignore */ } })
