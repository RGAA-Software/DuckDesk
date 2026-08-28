// 协议级 CDP 扩展测试:rustdesk FT 协议直连被控(不经产品页面代码)
// 覆盖 ft_cdp_test.mjs 触不到的用例:断点续传 / 覆盖确认(is_upload digest) / 目录上传含空目录 / 特殊字符文件名
// 原理:headless Chrome about:blank 页面里裸建 RTCPeerConnection + ft_data_channel,
//       信令走本地代理(同 ft_cdp_test),协议编解码在 Node 侧用 protobufjs 直接解析仓库 proto。
// 用法: node test/ft_ext_test.mjs
//   环境变量: FT_TARGET_BASE / FT_DEVICE_ID / FT_PWD_MD5 / FT_DIR(默认 C:/ft_test_data)
import { spawn } from 'node:child_process'
import { createHash, randomBytes } from 'node:crypto'
import { createServer } from 'node:http'
import { readFile } from 'node:fs/promises'
import zlib from 'node:zlib'
import os from 'node:os'
import path from 'node:path'
import protobuf from 'protobufjs'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const CDP_PORT = 9226
const TARGET_BASE = process.env.FT_TARGET_BASE || 'http://10.0.0.90:20371'
const PWD_MD5 = process.env.FT_PWD_MD5 || ''
const REMOTE_DIR = process.env.FT_DIR || 'C:/ft_test_data'
const BLOCK = 120 * 1024 // 对齐 file_transfer.ts FT_BLOCK_SIZE

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const sha256 = (buf) => createHash('sha256').update(buf).digest('hex')
let PASS = 0
let FAIL = 0
function ok(msg) { PASS++; console.log(`  OK: ${msg}`) }
function bad(msg) { FAIL++; console.log(`  FAIL: ${msg}`) }
function assert(cond, msg) { if (!cond) { bad(msg); throw new Error(`断言失败: ${msg}`) } ok(msg) }

// ---------- proto ----------
const protoDir = path.join(import.meta.dirname, '../proto')
const root = new protobuf.Root()
protobuf.parse(await readFile(path.join(protoDir, 'px_signaling_message.proto'), 'utf8'), root)
protobuf.parse(await readFile(path.join(protoDir, 'px_file_transfer.proto'), 'utf8'), root)
protobuf.parse(
  (await readFile(path.join(protoDir, 'px_message.proto'), 'utf8')).replace(/^\s*import\s+"[^"]+"\s*;$/gm, ''),
  root,
)
const PxMessage = root.lookupType('px.Message')
const MSG_ACTION = 270
const MSG_RESPONSE = 280
const enc = (fields) => PxMessage.encode(PxMessage.create(fields)).finish()
const dec = (payload) => PxMessage.decode(payload)
const num = (v) => (v == null ? 0 : Number(v))

// ---------- TLV(对齐 web/px_web_client/src/rtc/tlv.ts) ----------
const TLV_HDR = 32
function packTlv(payload, pktIndex) {
  const buf = new ArrayBuffer(TLV_HDR + payload.length)
  const v = new DataView(buf)
  v.setUint32(0, 1, true)
  v.setUint32(4, payload.length, true)
  v.setUint32(8, 0, true)
  v.setUint32(12, payload.length, true)
  v.setBigUint64(16, BigInt(pktIndex), true)
  v.setUint32(24, payload.length, true)
  new Uint8Array(buf, TLV_HDR).set(payload)
  return buf
}
class Reassembler {
  frag = null
  got = 0
  feed(buf) {
    const out = []
    if (buf.byteLength < TLV_HDR) return out
    const v = new DataView(buf)
    const type = v.getUint32(0, true)
    const len = v.getUint32(4, true)
    if (len > buf.byteLength - TLV_HDR) return out
    const payload = new Uint8Array(buf, TLV_HDR, len)
    if (type === 1) { this.frag = null; this.got = 0; out.push(payload); return out }
    const begin = v.getUint32(8, true)
    const parentLen = v.getUint32(24, true)
    if (type === 2 || !this.frag || this.frag.length !== parentLen) { this.frag = new Uint8Array(parentLen); this.got = 0 }
    if (begin + len > this.frag.length) { this.frag = null; this.got = 0; return out }
    this.frag.set(payload, begin)
    this.got += len
    if (this.got >= this.frag.length) { out.push(this.frag); this.frag = null; this.got = 0 }
    return out
  }
}

// ---------- 信令代理(only /alloc) ----------
async function startProxy() {
  const server = createServer((req, res) => {
    const url = new URL(req.url || '/', 'http://x')
    if (url.pathname === '/') { res.writeHead(200, { 'content-type': 'text/html' }); res.end('<html><body>ft ext</body></html>'); return }
    if (!url.pathname.startsWith('/alloc')) { res.writeHead(404); res.end(); return }
    const chunks = []
    req.on('data', (c) => chunks.push(c))
    req.on('end', () => {
      fetch(`${TARGET_BASE}${url.pathname}${url.search}`, {
        method: req.method,
        headers: { 'content-type': 'application/json' },
        body: Buffer.concat(chunks),
      })
        .then(async (r) => { res.writeHead(r.status, { 'content-type': 'application/json' }); res.end(Buffer.from(await r.arrayBuffer())) })
        .catch((e) => { res.writeHead(502); res.end(String(e)) })
    })
  })
  await new Promise((r) => server.listen(0, '127.0.0.1', r))
  return { server, port: server.address().port }
}

// ---------- CDP ----------
let msgId = 0
const pending = new Map()
let ws
function cdpSend(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++msgId
    pending.set(id, { resolve, reject })
    ws.send(JSON.stringify({ id, method, params }))
  })
}
async function evaluate(expression) {
  const r = await cdpSend('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })
  if (r.exceptionDetails) throw new Error(`页面内执行出错: ${JSON.stringify(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text)}`)
  return r.result?.value
}

// 页面侧管道:裸 RTCPeerConnection + ft_data_channel,收发经 base64 与 Node 桥接
const PAGE_PIPE = `
window.__st = { rx: [], open: false, closed: false, err: null }
window.__connect = async (proxyPort, deviceId, streamId, pwdMd5) => {
  const pc = new RTCPeerConnection()
  const dc = pc.createDataChannel('ft_data_channel')
  dc.binaryType = 'arraybuffer'
  dc.onopen = () => { window.__st.open = true }
  dc.onclose = () => { window.__st.closed = true }
  dc.onerror = (e) => { window.__st.err = String(e) }
  dc.onmessage = (ev) => {
    const u8 = new Uint8Array(ev.data)
    let s = ''
    for (let i = 0; i < u8.length; i += 32768) s += String.fromCharCode.apply(null, u8.subarray(i, i + 32768))
    window.__st.rx.push(btoa(s))
  }
  window.__dc = dc
  const offer = await pc.createOffer()
  await pc.setLocalDescription(offer)
  await new Promise((resolve) => {
    if (pc.iceGatheringState === 'complete') return resolve()
    const t = setInterval(() => { if (pc.iceGatheringState === 'complete') { clearInterval(t); resolve() } }, 100)
  })
  const q = new URLSearchParams({ device_id: deviceId, stream_id: streamId, safety_pwd_md5: pwdMd5 })
  const resp = await fetch('http://127.0.0.1:' + proxyPort + '/alloc/local/rtc?' + q, {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ sdp: pc.localDescription.sdp }),
  })
  const j = await resp.json()
  if (j.code !== 200) return 'signal rejected: ' + JSON.stringify(j)
  await pc.setRemoteDescription({ type: 'answer', sdp: j.data.answer_sdp })
  return 'ok'
}
window.__send = (b64) => {
  const s = atob(b64)
  const u8 = new Uint8Array(s.length)
  for (let i = 0; i < s.length; i++) u8[i] = s.charCodeAt(i)
  window.__dc.send(u8.buffer)
  return window.__dc.bufferedAmount
}
window.__drain = () => { const r = window.__st.rx; window.__st.rx = []; return r }
window.__state = () => ({ open: window.__st.open, closed: window.__st.closed, err: window.__st.err, buffered: window.__dc ? window.__dc.bufferedAmount : -1, rx: window.__st.rx.length })
`

// ---------- 一条 FT 连接(页面管道 + Node 协议状态机) ----------
class FtLink {
  constructor(target) { this.target = target; this.pkt = 0; this.reasm = new Reassembler(); this.inbox = []; this.streamId = '' }
  async connect(proxyPort, deviceId, streamId) {
    this.streamId = streamId
    const r = await evaluate(`__connect(${proxyPort}, ${JSON.stringify(deviceId)}, ${JSON.stringify(streamId)}, ${JSON.stringify(PWD_MD5)})`)
    if (r !== 'ok') throw new Error(`信令失败: ${r}`)
    const deadline = Date.now() + 30000
    while (Date.now() < deadline) {
      const st = await evaluate('__state()')
      if (st.open) return
      if (st.err || st.closed) throw new Error(`通道失败: ${JSON.stringify(st)}`)
      await sleep(300)
    }
    throw new Error('ft_data_channel 打开超时')
  }
  // px::Message.stream_id 必须带上(与真实 web 客户端一致):被控按它给作业标记
  // 归属连接,断线清理 DisconnectCleanup(stream_id) 据此匹配,不带则作业变僵尸
  async sendAction(action) { await this.sendRaw(enc({ type: MSG_ACTION, streamId: this.streamId, fileAction: action })) }
  async sendResponse(response) { await this.sendRaw(enc({ type: MSG_RESPONSE, streamId: this.streamId, fileResponse: response })) }
  async sendRaw(payload) {
    const b64 = Buffer.from(packTlv(payload, ++this.pkt)).toString('base64')
    // 反压:bufferedAmount 超 4MB 等落(对齐 file_transfer.ts 水位);等落期间只查水位,绝不重发同一条消息
    const buffered = await evaluate(`__send(${JSON.stringify(b64)})`)
    if (buffered < 4 * 1024 * 1024) return
    for (;;) {
      await sleep(100)
      const st = await evaluate('__state()')
      if (st.buffered < 4 * 1024 * 1024) return
    }
  }
  // 收取所有已到消息(非阻塞)
  async pump() {
    const raws = await evaluate('__drain()')
    for (const b64 of raws || []) {
      const buf = Buffer.from(b64, 'base64')
      for (const payload of this.reasm.feed(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength))) {
        const m = dec(payload)
        if (process.env.FT_DEBUG) {
          const fr = m.fileResponse
          const fa = m.fileAction
          const kind = fr ? Object.keys(fr).find((k) => fr[k] != null) : fa ? `action.${Object.keys(fa).find((k) => fa[k] != null)}` : '?'
          if (fr?.dir) {
            console.log(`    << dir path=${fr.dir.path} entries=[${fr.dir.entries.map((e) => `${e.name}(${Number(e.size)})`).join(', ')}]`)
          } else {
            console.log(`    << ${kind}`, JSON.stringify(m, (k, v) => (typeof v === 'bigint' ? Number(v) : v)).slice(0, 220))
          }
        }
        this.inbox.push(m)
      }
    }
  }
  // 等一条满足条件的消息
  async waitMsg(pred, timeoutMs, desc) {
    const deadline = Date.now() + timeoutMs
    while (Date.now() < deadline) {
      await this.pump()
      const ei = this.inbox.findIndex((x) => x.fileResponse?.error)
      if (ei >= 0) {
        const e = this.inbox.splice(ei, 1)[0].fileResponse.error
        throw new Error(`对端报错(等待 ${desc} 时): id=${e.id} ${e.error}`)
      }
      const i = this.inbox.findIndex(pred)
      if (i >= 0) return this.inbox.splice(i, 1)[0]
      await sleep(100)
    }
    await this.pump()
    throw new Error(`等待消息超时: ${desc}; inbox=${JSON.stringify(this.inbox).slice(0, 500)}`)
  }
  async listDir(p) {
    await this.sendAction({ readDir: { path: p, includeHidden: false } })
    // 按 path 精确匹配:send(下载)也会回 dir 消息,防止取到陈旧回包
    const m = await this.waitMsg((x) => x.fileResponse?.dir && x.fileResponse.dir.path === p, 15000, `dir ${p}`)
    return m.fileResponse.dir
  }
  async createDir(p) {
    await this.sendAction({ create: { id: 0, path: p } })
    await this.waitMsg((x) => x.fileResponse?.done || x.fileResponse?.error, 15000, `createDir ${p}`)
      .then((m) => { if (m.fileResponse.error) throw new Error(`createDir 失败: ${m.fileResponse.error.error}`) })
  }
  async removeDir(p, recursive) {
    await this.sendAction({ removeDir: { id: 0, path: p, recursive } })
    await this.waitMsg((x) => x.fileResponse?.done || x.fileResponse?.error, 15000, `removeDir ${p}`)
      .then((m) => { if (m.fileResponse.error) throw new Error(`removeDir 失败: ${m.fileResponse.error.error}`) })
  }
  async removeFile(p) {
    await this.sendAction({ removeFile: { id: 0, path: p, fileNum: 0 } })
    await this.waitMsg((x) => x.fileResponse?.done || x.fileResponse?.error, 15000, `removeFile ${p}`)
      .then((m) => { if (m.fileResponse.error) throw new Error(`removeFile 失败: ${m.fileResponse.error.error}`) })
  }
}

let nextJobId = 1

// 上传 done 后等远端文件可见(被控写侧 finalize 与回包存在毫秒级竞态,见测试报告)
async function waitRemoteFile(link, dir, name, size, timeoutMs = 10000) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    const d = await link.listDir(dir)
    const e = d.entries.find((x) => x.name === name)
    if (!e && process.env.FT_DEBUG) {
      for (const x of d.entries) {
        if (x.name.includes(name.slice(-12)) || name.includes(x.name.slice(-12))) {
          console.log(`    ?? 名称近似但不等: got=${JSON.stringify(x.name)} want=${JSON.stringify(name)} gotLen=${x.name.length} wantLen=${name.length}`)
        }
      }
    }
    if (e && (size == null || num(e.size) === size)) return e
    await sleep(300)
  }
  throw new Error(`远端文件未在 ${timeoutMs}ms 内可见: ${dir}/${name}`)
}

// 上传单个文件(顶层项 name='';receive.path 含文件名)
// opts: { isResume, abortAfterBytes(送这么多就裸中断,不发 EOF/done), onDigestUpload(收到 is_upload digest 时回调决策 'overwrite'|'skip') }
// 返回 { offset, aborted } ;未中断则表示传完
async function uploadOne(link, remotePath, data, mtime, opts = {}) {
  const id = nextJobId++
  await link.sendAction({
    receive: {
      id, path: remotePath, fileNum: 0, totalSize: data.length,
      files: [{ entryType: 4, name: '', size: data.length, modifiedTime: mtime }],
    },
  })
  await link.sendResponse({ digest: { id, fileNum: 0, lastModified: mtime, fileSize: data.length, isResume: !!opts.isResume } })
  // 等写侧决策:send_confirm(自动)或 digest is_upload(需主控决策)
  const m = await link.waitMsg(
    (x) => (x.fileAction?.sendConfirm && x.fileAction.sendConfirm.id === id) ||
           (x.fileResponse?.digest?.isUpload && x.fileResponse.digest.id === id),
    30000, 'send_confirm 或 is_upload digest')
  let offset = 0
  let bounced = null
  if (m.fileAction?.sendConfirm) {
    const c = m.fileAction.sendConfirm
    if (c.skip) return { offset: 0, skipped: true }
    offset = num(c.offsetBlk)
  } else {
    // is_upload digest:由"主控 UI"(测试脚本)决策
    const d = m.fileResponse.digest
    const decision = opts.onDigestUpload ? opts.onDigestUpload(d) : 'overwrite'
    bounced = d
    if (decision === 'skip') {
      await link.sendAction({ sendConfirm: { id, fileNum: 0, skip: true } })
      return { offset: 0, skipped: true, bouncedDigest: d }
    }
    await link.sendAction({ sendConfirm: { id, fileNum: 0, offsetBlk: 0 } })
    offset = 0
  }
  const limit = opts.abortAfterBytes != null ? Math.min(opts.abortAfterBytes, data.length) : data.length
  let pos = offset
  while (pos < limit) {
    const end = Math.min(pos + BLOCK, limit)
    await link.sendResponse({ block: { id, fileNum: 0, data: data.subarray(pos, end), compressed: false } })
    pos = end
  }
  if (opts.abortAfterBytes != null && limit < data.length) return { offset, aborted: true, sent: pos }
  // EOF 空块 + done
  await link.sendResponse({ block: { id, fileNum: 0, data: new Uint8Array(0), compressed: false } })
  await link.sendResponse({ done: { id, fileNum: 1 } })
  return { offset, sent: pos, bouncedDigest: bounced }
}

// 下载单个文件,返回 Buffer
async function downloadOne(link, remotePath) {
  const id = nextJobId++
  await link.sendAction({ send: { id, path: remotePath, includeHidden: false, fileNum: 0, fileType: 0 } })
  // rustdesk 语义:读侧先回 dir(文件清单)再回 digest;dir 留在 inbox,由 listDir 的 path 精确匹配避开
  const dm = await link.waitMsg((x) => x.fileResponse?.digest && !x.fileResponse.digest.isUpload && x.fileResponse.digest.id === id, 30000, '下载 digest')
  const d = dm.fileResponse.digest
  const total = num(d.fileSize)
  await link.sendAction({ sendConfirm: { id, fileNum: d.fileNum, offsetBlk: 0 } })
  const chunks = []
  let got = 0
  const deadline = Date.now() + 120000
  while (got < total) {
    if (Date.now() > deadline) throw new Error('下载块超时')
    const bm = await link.waitMsg((x) => (x.fileResponse?.block && x.fileResponse.block.id === id) || (x.fileResponse?.error), 30000, '下载块')
    if (bm.fileResponse.error) throw new Error(`下载失败: ${bm.fileResponse.error.error}`)
    const b = bm.fileResponse.block
    if (b.fileNum !== d.fileNum) continue
    const raw = Buffer.from(b.data)
    const data = b.compressed ? zlib.inflateSync(raw) : raw
    if (data.length > 0) { chunks.push(data); got += data.length }
  }
  // 收 done(可能已在 inbox)
  await link.waitMsg((x) => x.fileResponse?.done && x.fileResponse.done.id === id, 15000, '下载 done').catch(() => {})
  return Buffer.concat(chunks)
}

// 伪随机不可压缩数据(确定性种子,便于重传同内容)
function prngBuf(size, seed) {
  const out = Buffer.alloc(size)
  let x = seed >>> 0
  for (let i = 0; i < size; i += 4) {
    x ^= x << 13; x >>>= 0; x ^= x >> 17; x ^= x << 5; x >>>= 0
    out.writeUInt32LE(x, i)
  }
  return out
}

// ---------- 主流程 ----------
async function main() {
  const cfg = await (await fetch(`${TARGET_BASE}/get/render/configuration`, { signal: AbortSignal.timeout(5000) })).json()
  const deviceId = cfg?.data?.device_id || process.env.FT_DEVICE_ID || '001190520'
  console.log(`目标: ${TARGET_BASE} device_id=${deviceId} app=${cfg?.data?.app_version} 测试目录: ${REMOTE_DIR}`)

  const { server, port } = await startProxy()
  const userDataDir = path.join(os.tmpdir(), `ft_ext_chrome_${Date.now()}`)
  const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${userDataDir}`,
    '--no-first-run', '--disable-gpu', 'about:blank'])
  const cleanup = async () => {
    try { ws?.close() } catch { /* ignore */ }
    try { chrome.kill() } catch { /* ignore */ }
    server.close()
    await import('node:fs/promises').then((fs) => fs.rm(userDataDir, { recursive: true, force: true }).catch(() => {}))
  }

  try {
    let version = null
    for (let i = 0; i < 30; i++) {
      try { version = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json(); break } catch { await sleep(500) }
    }
    if (!version) throw new Error('CDP 端口未就绪')

    // 打开代理源页面(fetch 信令需同源;about:blank opaque origin 会被拦)
    const target = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(`http://127.0.0.1:${port}/`)}`, { method: 'PUT' })).json()
    ws = new WebSocket(target.webSocketDebuggerUrl)
    ws.onmessage = (ev) => {
      const m = JSON.parse(ev.data)
      if (m.id && pending.has(m.id)) { const p = pending.get(m.id); pending.delete(m.id); m.error ? p.reject(new Error(m.error.message)) : p.resolve(m.result) }
    }
    await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej })
    await cdpSend('Runtime.enable')
    await evaluate(PAGE_PIPE)

    const link = new FtLink(target)
    console.log('\n[0] 建立 FT 通道 ...')
    await link.connect(port, deviceId, `x${Date.now() % 1000000}`)
    ok('ft_data_channel 已打开(裸协议连接)')

    const ONLY = process.env.FT_ONLY ? Number(process.env.FT_ONLY) : 0
    // ---- 用例 1:特殊字符文件名 ----
    if (ONLY === 0 || ONLY === 1) {
    console.log('\n[1] 中文/空格/特殊字符文件名上传+下载回验 ...')
    const name1 = `中文 空格 #pecial (v2) &% ${Date.now() % 100000}.txt`
    const data1 = Buffer.from(`特殊文件名 smoke ${Date.now()} 中文内容\n`, 'utf8')
    await uploadOne(link, `${REMOTE_DIR}/${name1}`, data1, Math.floor(Date.now() / 1000))
    await waitRemoteFile(link, REMOTE_DIR, name1, data1.length)
    const dl1 = await downloadOne(link, `${REMOTE_DIR}/${name1}`)
    if (!dl1.equals(data1)) {
      console.log(`  不一致: 期望 ${data1.length}B sha=${sha256(data1)}, 实收 ${dl1.length}B sha=${sha256(dl1)}`)
      console.log('  期望 hex:', data1.toString('hex'))
      console.log('  实收 hex:', dl1.toString('hex'))
    }
    assert(dl1.equals(data1), `特殊字符文件 roundtrip sha256=${sha256(data1).slice(0, 16)}...`)
    const dir1 = await link.listDir(REMOTE_DIR)
    console.log('  远端条目:', dir1.entries.map((e) => e.name).join(' | '))
    assert(dir1.entries.some((e) => e.name === name1), 'listDir 可见特殊字符文件')
    await link.removeFile(`${REMOTE_DIR}/${name1}`)
    ok('远端文件已清理')

    // ---- 用例 2:目录上传含空目录 ----
    console.log('\n[2] 目录上传(含空目录) ...')
    const top = `${REMOTE_DIR}/ext_dir`
    await link.createDir(top)
    await link.createDir(`${top}/空目录 empty`)
    await link.createDir(`${top}/sub`)
    // 多文件一个作业:name 为相对路径(对齐 uploadFolder 语义)
    const f1 = Buffer.from('file at root\n')
    const f2 = prngBuf(300 * 1024, 777)
    const id2 = nextJobId++
    const mt = Math.floor(Date.now() / 1000)
    await link.sendAction({
      receive: {
        id: id2, path: top, fileNum: 0, totalSize: f1.length + f2.length,
        files: [
          { entryType: 4, name: 'a.txt', size: f1.length, modifiedTime: mt },
          { entryType: 4, name: 'sub/b.bin', size: f2.length, modifiedTime: mt },
        ],
      },
    })
    for (const [i, buf] of [[0, f1], [1, f2]]) {
      await link.sendResponse({ digest: { id: id2, fileNum: i, lastModified: mt, fileSize: buf.length, isResume: false } })
      const cm = await link.waitMsg((x) => x.fileAction?.sendConfirm && x.fileAction.sendConfirm.id === id2, 30000, `文件${i} confirm`)
      assert(!cm.fileAction.sendConfirm.skip, `文件${i} 未被 skip`)
      let pos = 0
      while (pos < buf.length) {
        const end = Math.min(pos + BLOCK, buf.length)
        await link.sendResponse({ block: { id: id2, fileNum: i, data: buf.subarray(pos, end), compressed: false } })
        pos = end
      }
      await link.sendResponse({ block: { id: id2, fileNum: i, data: new Uint8Array(0), compressed: false } })
    }
    await link.sendResponse({ done: { id: id2, fileNum: 2 } })
    await waitRemoteFile(link, top, 'a.txt', f1.length)
    await waitRemoteFile(link, `${top}/sub`, 'b.bin', f2.length)
    const dirTop = await link.listDir(top)
    assert(dirTop.entries.some((e) => e.name === '空目录 empty' && e.entryType === 0), '空目录存在于远端')
    assert(dirTop.entries.some((e) => e.name === 'a.txt'), '顶层文件 a.txt 存在')
    const dirSub = await link.listDir(`${top}/sub`)
    assert(dirSub.entries.some((e) => e.name === 'b.bin' && num(e.size) === f2.length), '子目录文件 sub/b.bin 存在且大小一致')
    const dl2 = await downloadOne(link, `${top}/sub/b.bin`)
    assert(dl2.equals(f2), 'sub/b.bin 下载回验一致')
    // rustdesk 语义:recursive remove_dir 只递归删空目录(fs.rs:1397),文件须先逐个删
    await link.removeFile(`${top}/a.txt`)
    await link.removeFile(`${top}/sub/b.bin`)
    await link.removeDir(top, true)
    await sleep(800)
    const dirGone = await link.listDir(REMOTE_DIR)
    assert(!dirGone.entries.some((e) => e.name === 'ext_dir'), '测试目录已递归删除(文件先删+空目录递归删)')
    } // end ONLY 1/2 (case 1-2 同页顺序执行)

    // ---- 用例 3:覆盖确认(同名不同内容 -> is_upload digest -> 覆盖) ----
    if (ONLY === 0 || ONLY === 3) {
    console.log('\n[3] 覆盖确认:同名不同内容 ...')
    const name3 = 'overwrite.bin'
    const dataA = prngBuf(1024 * 1024, 111)
    const dataB = prngBuf(512 * 1024, 222) // 不同大小不同内容
    await uploadOne(link, `${REMOTE_DIR}/${name3}`, dataA, 1000000)
    await waitRemoteFile(link, REMOTE_DIR, name3, dataA.length)
    const r3 = await uploadOne(link, `${REMOTE_DIR}/${name3}`, dataB, 2000000, {
      onDigestUpload: (d) => {
        ok(`收到 is_upload=true digest (is_identical=${d.isIdentical}, remote size=${num(d.fileSize)})`)
        return 'overwrite'
      },
    })
    assert(!!r3.bouncedDigest, '第二次上传确实走了 is_upload digest 决策分支')
    await waitRemoteFile(link, REMOTE_DIR, name3, dataB.length)
    const dl3 = await downloadOne(link, `${REMOTE_DIR}/${name3}`)
    assert(dl3.equals(dataB), '覆盖后内容为新内容 sha256 一致')
    await link.removeFile(`${REMOTE_DIR}/${name3}`)
    } // end ONLY 3

    // ---- 用例 4:50MB 断点续传 ----
    if (ONLY === 0 || ONLY === 4) {
    console.log('\n[4] 50MB 上传中断 -> is_resume 续传 -> hash 校验 ...')
    const bigSize = 50 * 1024 * 1024
    const big = prngBuf(bigSize, 424242)
    const bigHash = sha256(big)
    const bigName = `resume50_${Date.now() % 1000000}.bin` // 每轮唯一名,避免历史残留/句柄锁污染
    const bigPath = `${REMOTE_DIR}/${bigName}`
    const fixedMtime = 1700000000 // 固定 mtime,保证两轮 identical
    // 第一轮:传 ~20MB 后裸杀页面(模拟断线)
    const abortAt = 20 * 1024 * 1024
    const r4a = await uploadOne(link, bigPath, big, fixedMtime, { abortAfterBytes: abortAt })
    assert(r4a.aborted && r4a.sent === abortAt, `第一轮已发送 ${r4a.sent / 1024 / 1024}MB 后中断`)
    // 传输中现场:.download/.digest 是否都已落盘(等 2s 让被控 worker 把缓冲块写完)
    await sleep(2000)
    const dirMid = await link.listDir(REMOTE_DIR)
    const resumeEntries = dirMid.entries.filter((e) => e.name === bigName
      || e.name === `${bigName}.download`
      || e.name === `${bigName}.digest`)
    console.log(`  中断后(页面未关)目录共 ${dirMid.entries.length} 项,本次断点条目:`,
      resumeEntries.map((e) => `${e.name}(${Number(e.size)})`).join(' | '))
    console.log('  关闭页面(杀 WebRTC 连接) ...')
    await fetch(`http://127.0.0.1:${CDP_PORT}/json/close/${target.id}`).catch(() => {})
    await sleep(4000)

    // 第二轮:新页面新连接,is_resume=true
    const target2 = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(`http://127.0.0.1:${port}/`)}`, { method: 'PUT' })).json()
    const ws2 = new WebSocket(target2.webSocketDebuggerUrl)
    ws2.onmessage = (ev) => {
      const m = JSON.parse(ev.data)
      if (m.id && pending.has(m.id)) { const p = pending.get(m.id); pending.delete(m.id); m.error ? p.reject(new Error(m.error.message)) : p.resolve(m.result) }
    }
    await new Promise((res, rej) => { ws2.onopen = res; ws2.onerror = rej })
    ws.close()
    ws = ws2
    await cdpSend('Runtime.enable')
    await evaluate(PAGE_PIPE)
    const link2 = new FtLink(target2)
    await link2.connect(port, deviceId, `y${Date.now() % 1000000}`)
    ok('第二条 ft 通道已建立')
    // 断点现场检查:.download/.digest 凭证应保留(断线保留供续传,plan §2 取消语义)
    let dlTmp = null
    let dgTmp = null
    for (let i = 0; i < 20 && (!dlTmp || !dgTmp); i++) {
      const d = await link2.listDir(REMOTE_DIR)
      dlTmp = dlTmp || d.entries.find((e) => e.name === `${bigName}.download`)
      dgTmp = dgTmp || d.entries.find((e) => e.name === `${bigName}.digest`)
      if (!dlTmp || !dgTmp) await sleep(500)
    }
    assert(!!dlTmp && num(dlTmp.size) > 0, `断点后 .download 残留存在 (${dlTmp ? num(dlTmp.size) : '-'} bytes)`)
    assert(!!dgTmp, '断点后 .digest 凭证存在')
    const t0 = Date.now()
    const r4b = await uploadOne(link2, bigPath, big, fixedMtime, { isResume: true })
    assert(!r4b.skipped, '第二轮未被 skip')
    assert(r4b.offset > 0 && r4b.offset < bigSize, `续传 offset=${r4b.offset}(在途缓冲冲刷后可大于 sender 停点 ${abortAt},但 < 总大小)`)
    console.log(`  续传完成,耗时 ${((Date.now() - t0) / 1000).toFixed(1)}s`)
    const dl4 = await downloadOne(link2, bigPath)
    assert(dl4.length === bigSize, `下载大小一致 (${bigSize})`)
    assert(sha256(dl4) === bigHash, `50MB 续传后 sha256 一致 (${bigHash.slice(0, 16)}...)`)
    // 续传完成后 .download/.digest 应被清理(modify_time:删 digest、rename)
    let leftover = true
    for (let i = 0; i < 20 && leftover; i++) {
      const d = await link2.listDir(REMOTE_DIR)
      leftover = d.entries.some((e) => e.name === `${bigName}.download` || e.name === `${bigName}.digest`)
      if (leftover) await sleep(500)
    }
    assert(!leftover, '完成后 .download/.digest 已清理')
    await link2.removeFile(bigPath)
    ok('50MB 测试文件已删除')
    } // end ONLY 4

    console.log(`\n结果: PASS=${PASS} FAIL=${FAIL}`)
    return FAIL > 0 ? 1 : 0
  } finally {
    await cleanup()
  }
}

main()
  .then((exitCode) => { process.exitCode = exitCode })
  .catch((err) => {
    console.error(`\n测试失败(PASS=${PASS} FAIL=${FAIL}):`, err)
    process.exitCode = 1
  })
