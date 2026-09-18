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
const TARGET_BASE = process.env.FT_TARGET_BASE
if (!TARGET_BASE) throw new Error('FT_TARGET_BASE must be set to the current Render descriptor endpoint')
const PWD_MD5 = process.env.FT_PWD_MD5 || ''
const REMOTE_DIR = process.env.FT_DIR || 'C:/ft_test_data'
const BLOCK = 120 * 1024 // 对齐 file_transfer.ts FT_BLOCK_SIZE

const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds))
const sha256 = (buffer) => createHash('sha256').update(buffer).digest('hex')
let PASS = 0
let FAIL = 0
function recordPass(message) { PASS++; console.log(`  OK: ${message}`) }
function recordFailure(message) { FAIL++; console.log(`  FAIL: ${message}`) }
function assert(condition, message) {
  if (!condition) {
    recordFailure(message)
    throw new Error(`断言失败: ${message}`)
  }
  recordPass(message)
}

// ---------- proto ----------
const protoDir = path.join(import.meta.dirname, '../proto')
const root = new protobuf.Root()
protobuf.parse(await readFile(path.join(protoDir, 'px_file_transfer.proto'), 'utf8'), root)
protobuf.parse(
  (await readFile(path.join(protoDir, 'px_message.proto'), 'utf8')).replace(/^\s*import\s+"[^"]+"\s*;$/gm, ''),
  root,
)
const PxMessage = root.lookupType('px.Message')
const MSG_ACTION = 270
const MSG_RESPONSE = 280
const encodeProtocolMessage = (fields) => PxMessage.encode(PxMessage.create(fields)).finish()
const decodeProtocolMessage = (payload) => PxMessage.decode(payload)
const toNumber = (value) => (value == null ? 0 : Number(value))

// ---------- TLV(对齐 web/px_web_client/src/rtc/tlv.ts) ----------
const TLV_HDR = 32
function packTlv(payload, packetIndex) {
  const buffer = new ArrayBuffer(TLV_HDR + payload.length)
  const dataView = new DataView(buffer)
  dataView.setUint32(0, 1, true)
  dataView.setUint32(4, payload.length, true)
  dataView.setUint32(8, 0, true)
  dataView.setUint32(12, payload.length, true)
  dataView.setBigUint64(16, BigInt(packetIndex), true)
  dataView.setUint32(24, payload.length, true)
  new Uint8Array(buffer, TLV_HDR).set(payload)
  return buffer
}
class Reassembler {
  fragment = null
  receivedBytes = 0
  feed(buffer) {
    const payloads = []
    if (buffer.byteLength < TLV_HDR) return payloads
    const dataView = new DataView(buffer)
    const type = dataView.getUint32(0, true)
    const payloadLength = dataView.getUint32(4, true)
    if (payloadLength > buffer.byteLength - TLV_HDR) return payloads
    const payload = new Uint8Array(buffer, TLV_HDR, payloadLength)
    if (type === 1) { this.fragment = null; this.receivedBytes = 0; payloads.push(payload); return payloads }
    const beginOffset = dataView.getUint32(8, true)
    const parentLength = dataView.getUint32(24, true)
    if (type === 2 || !this.fragment || this.fragment.length !== parentLength) {
      this.fragment = new Uint8Array(parentLength)
      this.receivedBytes = 0
    }
    if (beginOffset + payloadLength > this.fragment.length) {
      this.fragment = null
      this.receivedBytes = 0
      return payloads
    }
    this.fragment.set(payload, beginOffset)
    this.receivedBytes += payloadLength
    if (this.receivedBytes >= this.fragment.length) {
      payloads.push(this.fragment)
      this.fragment = null
      this.receivedBytes = 0
    }
    return payloads
  }
}

// ---------- 信令代理(only /alloc) ----------
async function startProxy() {
  const server = createServer((req, res) => {
    const url = new URL(req.url || '/', 'http://x')
    if (url.pathname === '/') { res.writeHead(200, { 'content-type': 'text/html' }); res.end('<html><body>ft ext</body></html>'); return }
    if (!url.pathname.startsWith('/alloc')) { res.writeHead(404); res.end(); return }
    const chunks = []
    req.on('data', (chunk) => chunks.push(chunk))
    req.on('end', () => {
      fetch(`${TARGET_BASE}${url.pathname}${url.search}`, {
        method: req.method,
        headers: { 'content-type': 'application/json' },
        body: Buffer.concat(chunks),
      })
        .then(async (response) => { res.writeHead(response.status, { 'content-type': 'application/json' }); res.end(Buffer.from(await response.arrayBuffer())) })
        .catch((error) => { res.writeHead(502); res.end(String(error)) })
    })
  })
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
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
  const evaluationResult = await cdpSend('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })
  if (evaluationResult.exceptionDetails) {
    throw new Error(`页面内执行出错: ${JSON.stringify(evaluationResult.exceptionDetails.exception?.description ?? evaluationResult.exceptionDetails.text)}`)
  }
  return evaluationResult.result?.value
}

// 页面侧管道:裸 RTCPeerConnection + ft_data_channel,收发经 base64 与 Node 桥接
const PAGE_PIPE = `
window.__st = { rx: [], open: false, closed: false, err: null }
window.__connect = async (proxyPort, deviceId, streamId, pwdMd5) => {
  const peerConnection = new RTCPeerConnection()
  const dataChannel = peerConnection.createDataChannel('ft_data_channel')
  dataChannel.binaryType = 'arraybuffer'
  dataChannel.onopen = () => { window.__st.open = true }
  dataChannel.onclose = () => { window.__st.closed = true }
  dataChannel.onerror = (error) => { window.__st.err = String(error) }
  dataChannel.onmessage = (messageEvent) => {
    const bytes = new Uint8Array(messageEvent.data)
    let binaryText = ''
    for (let byteOffset = 0; byteOffset < bytes.length; byteOffset += 32768) {
      binaryText += String.fromCharCode.apply(null, bytes.subarray(byteOffset, byteOffset + 32768))
    }
    window.__st.rx.push(btoa(binaryText))
  }
  window.__dc = dataChannel
  const offer = await peerConnection.createOffer()
  await peerConnection.setLocalDescription(offer)
  await new Promise((resolve) => {
    if (peerConnection.iceGatheringState === 'complete') return resolve()
    const gatheringTimer = setInterval(() => {
      if (peerConnection.iceGatheringState === 'complete') {
        clearInterval(gatheringTimer)
        resolve()
      }
    }, 100)
  })
  const queryParameters = new URLSearchParams({ device_id: deviceId, stream_id: streamId, safety_pwd_md5: pwdMd5 })
  const response = await fetch('http://127.0.0.1:' + proxyPort + '/alloc/local/rtc?' + queryParameters, {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ sdp: peerConnection.localDescription.sdp }),
  })
  const responseBody = await response.json()
  if (responseBody.code !== 200) return 'signal rejected: ' + JSON.stringify(responseBody)
  await peerConnection.setRemoteDescription({ type: 'answer', sdp: responseBody.data.answer_sdp })
  return 'ok'
}
window.__send = (base64Payload) => {
  const binaryText = atob(base64Payload)
  const bytes = new Uint8Array(binaryText.length)
  for (let byteIndex = 0; byteIndex < binaryText.length; byteIndex++) bytes[byteIndex] = binaryText.charCodeAt(byteIndex)
  window.__dc.send(bytes.buffer)
  return window.__dc.bufferedAmount
}
window.__drain = () => { const receivedPayloads = window.__st.rx; window.__st.rx = []; return receivedPayloads }
window.__state = () => ({ open: window.__st.open, closed: window.__st.closed, err: window.__st.err, buffered: window.__dc ? window.__dc.bufferedAmount : -1, rx: window.__st.rx.length })
`

// ---------- 一条 FT 连接(页面管道 + Node 协议状态机) ----------
class FtLink {
  constructor(target) {
    this.target = target
    this.packetIndex = 0
    this.reassembler = new Reassembler()
    this.inbox = []
    this.streamId = ''
  }
  async connect(proxyPort, deviceId, streamId) {
    this.streamId = streamId
    const connectionResult = await evaluate(`__connect(${proxyPort}, ${JSON.stringify(deviceId)}, ${JSON.stringify(streamId)}, ${JSON.stringify(PWD_MD5)})`)
    if (connectionResult !== 'ok') throw new Error(`信令失败: ${connectionResult}`)
    const deadline = Date.now() + 30000
    while (Date.now() < deadline) {
      const channelState = await evaluate('__state()')
      if (channelState.open) return
      if (channelState.err || channelState.closed) throw new Error(`通道失败: ${JSON.stringify(channelState)}`)
      await sleep(300)
    }
    throw new Error('ft_data_channel 打开超时')
  }
  // px::Message.stream_id 必须带上(与真实 web 客户端一致):被控按它给作业标记
  // 归属连接,断线清理 DisconnectCleanup(stream_id) 据此匹配,不带则作业变僵尸
  async sendAction(action) {
    await this.sendRaw(encodeProtocolMessage({ type: MSG_ACTION, streamId: this.streamId, fileAction: action }))
  }
  async sendResponse(response) {
    await this.sendRaw(encodeProtocolMessage({ type: MSG_RESPONSE, streamId: this.streamId, fileResponse: response }))
  }
  async sendRaw(payload) {
    const base64Payload = Buffer.from(packTlv(payload, ++this.packetIndex)).toString('base64')
    // 反压:bufferedAmount 超 4MB 等落(对齐 file_transfer.ts 水位);等落期间只查水位,绝不重发同一条消息
    const buffered = await evaluate(`__send(${JSON.stringify(base64Payload)})`)
    if (buffered < 4 * 1024 * 1024) return
    for (;;) {
      await sleep(100)
      const channelState = await evaluate('__state()')
      if (channelState.buffered < 4 * 1024 * 1024) return
    }
  }
  // 收取所有已到消息(非阻塞)
  async pump() {
    const rawPayloads = await evaluate('__drain()')
    for (const base64Payload of rawPayloads || []) {
      const buffer = Buffer.from(base64Payload, 'base64')
      for (const payload of this.reassembler.feed(buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength))) {
        const message = decodeProtocolMessage(payload)
        if (process.env.FT_DEBUG) {
          const fileResponse = message.fileResponse
          const fileAction = message.fileAction
          const kind = fileResponse
            ? Object.keys(fileResponse).find((key) => fileResponse[key] != null)
            : fileAction
              ? `action.${Object.keys(fileAction).find((key) => fileAction[key] != null)}`
              : '?'
          if (fileResponse?.dir) {
            console.log(`    << dir path=${fileResponse.dir.path} entries=[${fileResponse.dir.entries.map((entry) => `${entry.name}(${Number(entry.size)})`).join(', ')}]`)
          } else {
            console.log(`    << ${kind}`, JSON.stringify(message, (key, value) => (typeof value === 'bigint' ? Number(value) : value)).slice(0, 220))
          }
        }
        this.inbox.push(message)
      }
    }
  }
  // 等一条满足条件的消息
  async waitForMessage(predicate, timeoutMs, description) {
    const deadline = Date.now() + timeoutMs
    while (Date.now() < deadline) {
      await this.pump()
      const errorIndex = this.inbox.findIndex((message) => message.fileResponse?.error)
      if (errorIndex >= 0) {
        const errorResponse = this.inbox.splice(errorIndex, 1)[0].fileResponse.error
        throw new Error(`对端报错(等待 ${description} 时): id=${errorResponse.id} ${errorResponse.error}`)
      }
      const messageIndex = this.inbox.findIndex(predicate)
      if (messageIndex >= 0) return this.inbox.splice(messageIndex, 1)[0]
      await sleep(100)
    }
    await this.pump()
    throw new Error(`等待消息超时: ${description}; inbox=${JSON.stringify(this.inbox).slice(0, 500)}`)
  }
  async listDir(directoryPath) {
    await this.sendAction({ readDir: { path: directoryPath, includeHidden: false } })
    // 按 path 精确匹配:send(下载)也会回 dir 消息,防止取到陈旧回包
    const message = await this.waitForMessage(
      (candidateMessage) => candidateMessage.fileResponse?.dir && candidateMessage.fileResponse.dir.path === directoryPath,
      15000,
      `dir ${directoryPath}`,
    )
    return message.fileResponse.dir
  }
  async createDir(directoryPath) {
    await this.sendAction({ create: { id: 0, path: directoryPath } })
    await this.waitForMessage(
      (message) => message.fileResponse?.done || message.fileResponse?.error,
      15000,
      `createDir ${directoryPath}`,
    ).then((message) => {
      if (message.fileResponse.error) throw new Error(`createDir 失败: ${message.fileResponse.error.error}`)
    })
  }
  async removeDir(directoryPath, recursive) {
    await this.sendAction({ removeDir: { id: 0, path: directoryPath, recursive } })
    await this.waitForMessage(
      (message) => message.fileResponse?.done || message.fileResponse?.error,
      15000,
      `removeDir ${directoryPath}`,
    ).then((message) => {
      if (message.fileResponse.error) throw new Error(`removeDir 失败: ${message.fileResponse.error.error}`)
    })
  }
  async removeFile(filePath) {
    await this.sendAction({ removeFile: { id: 0, path: filePath, fileNum: 0 } })
    await this.waitForMessage(
      (message) => message.fileResponse?.done || message.fileResponse?.error,
      15000,
      `removeFile ${filePath}`,
    ).then((message) => {
      if (message.fileResponse.error) throw new Error(`removeFile 失败: ${message.fileResponse.error.error}`)
    })
  }
}

let nextJobId = 1

// 上传 done 后等远端文件可见(被控写侧 finalize 与回包存在毫秒级竞态,见测试报告)
async function waitRemoteFile(link, dir, name, size, timeoutMs = 10000) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    const directoryResponse = await link.listDir(dir)
    const matchingEntry = directoryResponse.entries.find((entry) => entry.name === name)
    if (!matchingEntry && process.env.FT_DEBUG) {
      for (const entry of directoryResponse.entries) {
        if (entry.name.includes(name.slice(-12)) || name.includes(entry.name.slice(-12))) {
          console.log(`    ?? 名称近似但不等: got=${JSON.stringify(entry.name)} want=${JSON.stringify(name)} gotLen=${entry.name.length} wantLen=${name.length}`)
        }
      }
    }
    if (matchingEntry && (size == null || toNumber(matchingEntry.size) === size)) return matchingEntry
    await sleep(300)
  }
  throw new Error(`远端文件未在 ${timeoutMs}ms 内可见: ${dir}/${name}`)
}

// 上传单个文件(顶层项 name='';receive.path 含文件名)
// opts: { isResume, abortAfterBytes(送这么多就裸中断,不发 EOF/done), onDigestUpload(收到 is_upload digest 时回调决策 'overwrite'|'skip') }
// 返回 { offset, aborted } ;未中断则表示传完
async function uploadOne(link, remotePath, data, modifiedTime, options = {}) {
  const id = nextJobId++
  await link.sendAction({
    receive: {
      id, path: remotePath, fileNum: 0, totalSize: data.length,
      files: [{ entryType: 4, name: '', size: data.length, modifiedTime }],
    },
  })
  await link.sendResponse({ digest: { id, fileNum: 0, lastModified: modifiedTime, fileSize: data.length, isResume: !!options.isResume } })
  // 等写侧决策:send_confirm(自动)或 digest is_upload(需主控决策)
  const responseMessage = await link.waitForMessage(
    (message) => (message.fileAction?.sendConfirm && message.fileAction.sendConfirm.id === id) ||
           (message.fileResponse?.digest?.isUpload && message.fileResponse.digest.id === id),
    30000, 'send_confirm 或 is_upload digest')
  let offset = 0
  let bounced = null
  if (responseMessage.fileAction?.sendConfirm) {
    const confirmation = responseMessage.fileAction.sendConfirm
    if (confirmation.skip) return { offset: 0, skipped: true }
    offset = toNumber(confirmation.offsetBlk)
  } else {
    // is_upload digest:由"主控 UI"(测试脚本)决策
    const digest = responseMessage.fileResponse.digest
    const decision = options.onDigestUpload ? options.onDigestUpload(digest) : 'overwrite'
    bounced = digest
    if (decision === 'skip') {
      await link.sendAction({ sendConfirm: { id, fileNum: 0, skip: true } })
      return { offset: 0, skipped: true, bouncedDigest: digest }
    }
    await link.sendAction({ sendConfirm: { id, fileNum: 0, offsetBlk: 0 } })
    offset = 0
  }
  const limit = options.abortAfterBytes != null ? Math.min(options.abortAfterBytes, data.length) : data.length
  let readOffset = offset
  while (readOffset < limit) {
    const blockEnd = Math.min(readOffset + BLOCK, limit)
    await link.sendResponse({ block: { id, fileNum: 0, data: data.subarray(readOffset, blockEnd), compressed: false } })
    readOffset = blockEnd
  }
  if (options.abortAfterBytes != null && limit < data.length) return { offset, aborted: true, sent: readOffset }
  // EOF 空块 + done
  await link.sendResponse({ block: { id, fileNum: 0, data: new Uint8Array(0), compressed: false } })
  await link.sendResponse({ done: { id, fileNum: 1 } })
  return { offset, sent: readOffset, bouncedDigest: bounced }
}

// 下载单个文件,返回 Buffer
async function downloadOne(link, remotePath) {
  const id = nextJobId++
  await link.sendAction({ send: { id, path: remotePath, includeHidden: false, fileNum: 0, fileType: 0 } })
  // rustdesk 语义:读侧先回 dir(文件清单)再回 digest;dir 留在 inbox,由 listDir 的 path 精确匹配避开
  const digestMessage = await link.waitForMessage(
    (message) => message.fileResponse?.digest && !message.fileResponse.digest.isUpload && message.fileResponse.digest.id === id,
    30000,
    '下载 digest',
  )
  const digest = digestMessage.fileResponse.digest
  const totalBytes = toNumber(digest.fileSize)
  await link.sendAction({ sendConfirm: { id, fileNum: digest.fileNum, offsetBlk: 0 } })
  const chunks = []
  let receivedBytes = 0
  const deadline = Date.now() + 120000
  while (receivedBytes < totalBytes) {
    if (Date.now() > deadline) throw new Error('下载块超时')
    const blockMessage = await link.waitForMessage(
      (message) => (message.fileResponse?.block && message.fileResponse.block.id === id) || message.fileResponse?.error,
      30000,
      '下载块',
    )
    if (blockMessage.fileResponse.error) throw new Error(`下载失败: ${blockMessage.fileResponse.error.error}`)
    const block = blockMessage.fileResponse.block
    if (block.fileNum !== digest.fileNum) continue
    const rawBytes = Buffer.from(block.data)
    const fileBytes = block.compressed ? zlib.inflateSync(rawBytes) : rawBytes
    if (fileBytes.length > 0) {
      chunks.push(fileBytes)
      receivedBytes += fileBytes.length
    }
  }
  // 收 done(可能已在 inbox)
  await link.waitForMessage(
    (message) => message.fileResponse?.done && message.fileResponse.done.id === id,
    15000,
    '下载 done',
  ).catch(() => {})
  return Buffer.concat(chunks)
}

// 伪随机不可压缩数据(确定性种子,便于重传同内容)
function createPseudoRandomBuffer(size, seed) {
  const buffer = Buffer.alloc(size)
  let randomState = seed >>> 0
  for (let byteOffset = 0; byteOffset < size; byteOffset += 4) {
    randomState ^= randomState << 13
    randomState >>>= 0
    randomState ^= randomState >> 17
    randomState ^= randomState << 5
    randomState >>>= 0
    buffer.writeUInt32LE(randomState, byteOffset)
  }
  return buffer
}

// ---------- 主流程 ----------
async function main() {
  const renderConfiguration = await (await fetch(`${TARGET_BASE}/get/render/configuration`, { signal: AbortSignal.timeout(5000) })).json()
  const deviceId = renderConfiguration?.data?.device_id || process.env.FT_DEVICE_ID || '001190520'
  console.log(`目标: ${TARGET_BASE} device_id=${deviceId} app=${renderConfiguration?.data?.app_version} 测试目录: ${REMOTE_DIR}`)

  const { server, port } = await startProxy()
  const userDataDir = path.join(os.tmpdir(), `ft_ext_chrome_${Date.now()}`)
  const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${userDataDir}`,
    '--no-first-run', '--disable-gpu', 'about:blank'])
  const cleanup = async () => {
    try { ws?.close() } catch { /* ignore */ }
    try { chrome.kill() } catch { /* ignore */ }
    server.close()
    await import('node:fs/promises').then((fileSystem) => fileSystem.rm(userDataDir, { recursive: true, force: true }).catch(() => {}))
  }

  try {
    let version = null
    for (let attemptNumber = 0; attemptNumber < 30; attemptNumber++) {
      try { version = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json(); break } catch { await sleep(500) }
    }
    if (!version) throw new Error('CDP 端口未就绪')

    // 打开代理源页面(fetch 信令需同源;about:blank opaque origin 会被拦)
    const target = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(`http://127.0.0.1:${port}/`)}`, { method: 'PUT' })).json()
    ws = new WebSocket(target.webSocketDebuggerUrl)
    ws.onmessage = (messageEvent) => {
      const message = JSON.parse(messageEvent.data)
      if (message.id && pending.has(message.id)) {
        const pendingRequest = pending.get(message.id)
        pending.delete(message.id)
        message.error ? pendingRequest.reject(new Error(message.error.message)) : pendingRequest.resolve(message.result)
      }
    }
    await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject })
    await cdpSend('Runtime.enable')
    await evaluate(PAGE_PIPE)

    const link = new FtLink(target)
    console.log('\n[0] 建立 FT 通道 ...')
    await link.connect(port, deviceId, `x${Date.now() % 1000000}`)
    recordPass('ft_data_channel 已打开(裸协议连接)')

    const ONLY = process.env.FT_ONLY ? Number(process.env.FT_ONLY) : 0
    // ---- 用例 1:特殊字符文件名 ----
    if (ONLY === 0 || ONLY === 1) {
    console.log('\n[1] 中文/空格/特殊字符文件名上传+下载回验 ...')
    const name1 = `中文 空格 #pecial (v2) &% ${Date.now() % 100000}.txt`
    const originalFileBytes = Buffer.from(`特殊文件名 smoke ${Date.now()} 中文内容\n`, 'utf8')
    await uploadOne(link, `${REMOTE_DIR}/${name1}`, originalFileBytes, Math.floor(Date.now() / 1000))
    await waitRemoteFile(link, REMOTE_DIR, name1, originalFileBytes.length)
    const downloadedFileBytes = await downloadOne(link, `${REMOTE_DIR}/${name1}`)
    if (!downloadedFileBytes.equals(originalFileBytes)) {
      console.log(`  不一致: 期望 ${originalFileBytes.length}B sha=${sha256(originalFileBytes)}, 实收 ${downloadedFileBytes.length}B sha=${sha256(downloadedFileBytes)}`)
      console.log('  期望 hex:', originalFileBytes.toString('hex'))
      console.log('  实收 hex:', downloadedFileBytes.toString('hex'))
    }
    assert(downloadedFileBytes.equals(originalFileBytes), `特殊字符文件 roundtrip sha256=${sha256(originalFileBytes).slice(0, 16)}...`)
    const dir1 = await link.listDir(REMOTE_DIR)
    console.log('  远端条目:', dir1.entries.map((entry) => entry.name).join(' | '))
    assert(dir1.entries.some((entry) => entry.name === name1), 'listDir 可见特殊字符文件')
    await link.removeFile(`${REMOTE_DIR}/${name1}`)
    recordPass('远端文件已清理')

    // ---- 用例 2:目录上传含空目录 ----
    console.log('\n[2] 目录上传(含空目录) ...')
    const top = `${REMOTE_DIR}/ext_dir`
    await link.createDir(top)
    await link.createDir(`${top}/空目录 empty`)
    await link.createDir(`${top}/sub`)
    // 多文件一个作业:name 为相对路径(对齐 uploadFolder 语义)
    const rootFileBytes = Buffer.from('file at root\n')
    const nestedFileBytes = createPseudoRandomBuffer(300 * 1024, 777)
    const directoryJobId = nextJobId++
    const modifiedTime = Math.floor(Date.now() / 1000)
    await link.sendAction({
      receive: {
        id: directoryJobId, path: top, fileNum: 0, totalSize: rootFileBytes.length + nestedFileBytes.length,
        files: [
          { entryType: 4, name: 'a.txt', size: rootFileBytes.length, modifiedTime },
          { entryType: 4, name: 'sub/b.bin', size: nestedFileBytes.length, modifiedTime },
        ],
      },
    })
    for (const [fileIndex, fileBytes] of [[0, rootFileBytes], [1, nestedFileBytes]]) {
      await link.sendResponse({
        digest: {
          id: directoryJobId,
          fileNum: fileIndex,
          lastModified: modifiedTime,
          fileSize: fileBytes.length,
          isResume: false,
        },
      })
      const confirmationMessage = await link.waitForMessage(
        (message) => message.fileAction?.sendConfirm && message.fileAction.sendConfirm.id === directoryJobId,
        30000,
        `文件${fileIndex} confirm`,
      )
      assert(!confirmationMessage.fileAction.sendConfirm.skip, `文件${fileIndex} 未被 skip`)
      let readOffset = 0
      while (readOffset < fileBytes.length) {
        const blockEnd = Math.min(readOffset + BLOCK, fileBytes.length)
        await link.sendResponse({
          block: {
            id: directoryJobId,
            fileNum: fileIndex,
            data: fileBytes.subarray(readOffset, blockEnd),
            compressed: false,
          },
        })
        readOffset = blockEnd
      }
      await link.sendResponse({ block: { id: directoryJobId, fileNum: fileIndex, data: new Uint8Array(0), compressed: false } })
    }
    await link.sendResponse({ done: { id: directoryJobId, fileNum: 2 } })
    await waitRemoteFile(link, top, 'a.txt', rootFileBytes.length)
    await waitRemoteFile(link, `${top}/sub`, 'b.bin', nestedFileBytes.length)
    const dirTop = await link.listDir(top)
    assert(dirTop.entries.some((entry) => entry.name === '空目录 empty' && entry.entryType === 0), '空目录存在于远端')
    assert(dirTop.entries.some((entry) => entry.name === 'a.txt'), '顶层文件 a.txt 存在')
    const dirSub = await link.listDir(`${top}/sub`)
    assert(
      dirSub.entries.some((entry) => entry.name === 'b.bin' && toNumber(entry.size) === nestedFileBytes.length),
      '子目录文件 sub/b.bin 存在且大小一致',
    )
    const downloadedNestedFile = await downloadOne(link, `${top}/sub/b.bin`)
    assert(downloadedNestedFile.equals(nestedFileBytes), 'sub/b.bin 下载回验一致')
    // rustdesk 语义:recursive remove_dir 只递归删空目录(fs.rs:1397),文件须先逐个删
    await link.removeFile(`${top}/a.txt`)
    await link.removeFile(`${top}/sub/b.bin`)
    await link.removeDir(top, true)
    await sleep(800)
    const dirGone = await link.listDir(REMOTE_DIR)
    assert(!dirGone.entries.some((entry) => entry.name === 'ext_dir'), '测试目录已递归删除(文件先删+空目录递归删)')
    } // end ONLY 1/2 (case 1-2 同页顺序执行)

    // ---- 用例 3:覆盖确认(同名不同内容 -> is_upload digest -> 覆盖) ----
    if (ONLY === 0 || ONLY === 3) {
    console.log('\n[3] 覆盖确认:同名不同内容 ...')
    const name3 = 'overwrite.bin'
    const originalBytes = createPseudoRandomBuffer(1024 * 1024, 111)
    const replacementBytes = createPseudoRandomBuffer(512 * 1024, 222) // 不同大小不同内容
    await uploadOne(link, `${REMOTE_DIR}/${name3}`, originalBytes, 1000000)
    await waitRemoteFile(link, REMOTE_DIR, name3, originalBytes.length)
    const overwriteResult = await uploadOne(link, `${REMOTE_DIR}/${name3}`, replacementBytes, 2000000, {
      onDigestUpload: (digest) => {
        recordPass(`收到 is_upload=true digest (is_identical=${digest.isIdentical}, remote size=${toNumber(digest.fileSize)})`)
        return 'overwrite'
      },
    })
    assert(!!overwriteResult.bouncedDigest, '第二次上传确实走了 is_upload digest 决策分支')
    await waitRemoteFile(link, REMOTE_DIR, name3, replacementBytes.length)
    const overwrittenDownload = await downloadOne(link, `${REMOTE_DIR}/${name3}`)
    assert(overwrittenDownload.equals(replacementBytes), '覆盖后内容为新内容 sha256 一致')
    await link.removeFile(`${REMOTE_DIR}/${name3}`)
    } // end ONLY 3

    // ---- 用例 4:50MB 断点续传 ----
    if (ONLY === 0 || ONLY === 4) {
    console.log('\n[4] 50MB 上传中断 -> is_resume 续传 -> hash 校验 ...')
    const bigSize = 50 * 1024 * 1024
    const largeFileBytes = createPseudoRandomBuffer(bigSize, 424242)
    const bigHash = sha256(largeFileBytes)
    const bigName = `resume50_${Date.now() % 1000000}.bin` // 每轮唯一名,避免历史残留/句柄锁污染
    const bigPath = `${REMOTE_DIR}/${bigName}`
    const fixedMtime = 1700000000 // 固定 mtime,保证两轮 identical
    // 第一轮:传 ~20MB 后裸杀页面(模拟断线)
    const abortAt = 20 * 1024 * 1024
    const interruptedUpload = await uploadOne(link, bigPath, largeFileBytes, fixedMtime, { abortAfterBytes: abortAt })
    assert(interruptedUpload.aborted && interruptedUpload.sent === abortAt, `第一轮已发送 ${interruptedUpload.sent / 1024 / 1024}MB 后中断`)
    // 传输中现场:.download/.digest 是否都已落盘(等 2s 让被控 worker 把缓冲块写完)
    await sleep(2000)
    const dirMid = await link.listDir(REMOTE_DIR)
    const resumeEntries = dirMid.entries.filter((entry) => entry.name === bigName
      || entry.name === `${bigName}.download`
      || entry.name === `${bigName}.digest`)
    console.log(`  中断后(页面未关)目录共 ${dirMid.entries.length} 项,本次断点条目:`,
      resumeEntries.map((entry) => `${entry.name}(${Number(entry.size)})`).join(' | '))
    console.log('  关闭页面(杀 WebRTC 连接) ...')
    await fetch(`http://127.0.0.1:${CDP_PORT}/json/close/${target.id}`).catch(() => {})
    await sleep(4000)

    // 第二轮:新页面新连接,is_resume=true
    const target2 = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(`http://127.0.0.1:${port}/`)}`, { method: 'PUT' })).json()
    const ws2 = new WebSocket(target2.webSocketDebuggerUrl)
    ws2.onmessage = (messageEvent) => {
      const message = JSON.parse(messageEvent.data)
      if (message.id && pending.has(message.id)) {
        const pendingRequest = pending.get(message.id)
        pending.delete(message.id)
        message.error ? pendingRequest.reject(new Error(message.error.message)) : pendingRequest.resolve(message.result)
      }
    }
    await new Promise((resolve, reject) => { ws2.onopen = resolve; ws2.onerror = reject })
    ws.close()
    ws = ws2
    await cdpSend('Runtime.enable')
    await evaluate(PAGE_PIPE)
    const link2 = new FtLink(target2)
    await link2.connect(port, deviceId, `y${Date.now() % 1000000}`)
    recordPass('第二条 ft 通道已建立')
    // 断点现场检查:.download/.digest 凭证应保留(断线保留供续传,plan §2 取消语义)
    let partialDownloadEntry = null
    let digestEntry = null
    for (let attemptNumber = 0; attemptNumber < 20 && (!partialDownloadEntry || !digestEntry); attemptNumber++) {
      const directoryResponse = await link2.listDir(REMOTE_DIR)
      partialDownloadEntry = partialDownloadEntry || directoryResponse.entries.find((entry) => entry.name === `${bigName}.download`)
      digestEntry = digestEntry || directoryResponse.entries.find((entry) => entry.name === `${bigName}.digest`)
      if (!partialDownloadEntry || !digestEntry) await sleep(500)
    }
    assert(
      !!partialDownloadEntry && toNumber(partialDownloadEntry.size) > 0,
      `断点后 .download 残留存在 (${partialDownloadEntry ? toNumber(partialDownloadEntry.size) : '-'} bytes)`,
    )
    assert(!!digestEntry, '断点后 .digest 凭证存在')
    const resumeStartedAt = Date.now()
    const resumedUpload = await uploadOne(link2, bigPath, largeFileBytes, fixedMtime, { isResume: true })
    assert(!resumedUpload.skipped, '第二轮未被 skip')
    assert(
      resumedUpload.offset > 0 && resumedUpload.offset < bigSize,
      `续传 offset=${resumedUpload.offset}(在途缓冲冲刷后可大于 sender 停点 ${abortAt},但 < 总大小)`,
    )
    console.log(`  续传完成,耗时 ${((Date.now() - resumeStartedAt) / 1000).toFixed(1)}s`)
    const downloadedLargeFile = await downloadOne(link2, bigPath)
    assert(downloadedLargeFile.length === bigSize, `下载大小一致 (${bigSize})`)
    assert(sha256(downloadedLargeFile) === bigHash, `50MB 续传后 sha256 一致 (${bigHash.slice(0, 16)}...)`)
    // 续传完成后 .download/.digest 应被清理(modify_time:删 digest、rename)
    let leftover = true
    for (let attemptNumber = 0; attemptNumber < 20 && leftover; attemptNumber++) {
      const directoryResponse = await link2.listDir(REMOTE_DIR)
      leftover = directoryResponse.entries.some((entry) => entry.name === `${bigName}.download` || entry.name === `${bigName}.digest`)
      if (leftover) await sleep(500)
    }
    assert(!leftover, '完成后 .download/.digest 已清理')
    await link2.removeFile(bigPath)
    recordPass('50MB 测试文件已删除')
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
