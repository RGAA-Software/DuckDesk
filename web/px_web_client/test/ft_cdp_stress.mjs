// 补充验证:大文件上传/下载(多块) + 超大目录(触发 render TLV 分片,验证重组)
import { spawn } from 'node:child_process'
import { createHash, randomBytes } from 'node:crypto'
import { readFile, rm } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const CDP_PORT = 9223
const PAGE_URL =
  'http://127.0.0.1:20371/web_client/?deviceId=600378210&streamId=ft2&pwd_md5=698d51a19d8a121ce581499d7b701668'
const UPLOAD_DIR = 'C:/Users/Public'
const UPLOAD_NAME = `ft_web_big_${Date.now()}.bin`
const BIG_SIZE = 4 * 1024 * 1024 + 12345 // 4MB+,64 块以上

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const sha256 = (buf) => createHash('sha256').update(buf).digest('hex')

function assert(cond, msg) {
  if (!cond) throw new Error(`断言失败: ${msg}`)
  console.log(`  OK: ${msg}`)
}

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
  const result = await cdpSend('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })
  if (result.exceptionDetails) {
    throw new Error(`页面内执行出错: ${JSON.stringify(result.exceptionDetails.exception?.description ?? result.exceptionDetails.text)}`)
  }
  return result.result?.value
}

async function main() {
  const userDataDir = path.join(os.tmpdir(), `ft_cdp_chrome_${Date.now()}`)
  const chrome = spawn(CHROME, [
    '--headless=new',
    `--remote-debugging-port=${CDP_PORT}`,
    `--user-data-dir=${userDataDir}`,
    '--no-first-run',
    '--disable-gpu',
    'about:blank',
  ])
  const cleanup = async () => {
    try { chrome.kill() } catch { /* ignore */ }
    await rm(userDataDir, { recursive: true, force: true }).catch(() => {})
  }
  try {
    for (let i = 0; i < 30; i++) {
      try {
        await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)
        break
      } catch {
        await sleep(500)
      }
    }
    const target = await (
      await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(PAGE_URL)}`, { method: 'PUT' })
    ).json()
    ws = new WebSocket(target.webSocketDebuggerUrl)
    ws.onmessage = (ev) => {
      const msg = JSON.parse(ev.data)
      if (msg.id && pending.has(msg.id)) {
        const p = pending.get(msg.id)
        pending.delete(msg.id)
        msg.error ? p.reject(new Error(msg.error.message)) : p.resolve(msg.result)
      }
    }
    await new Promise((resolve, reject) => {
      ws.onopen = resolve
      ws.onerror = reject
    })
    await cdpSend('Runtime.enable')

    console.log('[1] 等 ft 就绪 ...')
    const deadline = Date.now() + 60000
    while (Date.now() < deadline) {
      if (await evaluate('!!(window.__ft && window.__ft.ready())')) break
      await sleep(1000)
    }

    console.log('[2] 列超大目录 C:/Windows/System32(响应 >128KB,触发 TLV 分片重组)...')
    const big = await evaluate('window.__ft.listDir("C:/Windows/System32")')
    console.log(`  条目数: ${big.files.length}`)
    assert(big.files.length > 2000, `System32 条目数 ${big.files.length} > 2000(分片重组正确)`)
    const names = new Set(big.files.map((f) => f.name))
    assert(names.has('kernel32.dll') && names.has('notepad.exe'), '关键文件存在,重组数据无损')

    console.log('[3] 上传 4MB+ 随机二进制 ...')
    const bytes = randomBytes(BIG_SIZE)
    const b64 = bytes.toString('base64')
    const expected = sha256(bytes)
    const upResult = await evaluate(
      `(async () => {
        const bin = atob(${JSON.stringify(b64)})
        const arr = new Uint8Array(bin.length)
        for (let i = 0; i < bin.length; i++) arr[i] = bin.charCodeAt(i)
        const file = new File([arr.buffer], ${JSON.stringify(UPLOAD_NAME)})
        const cli = window.__ft
        // 直接走内部 client 以支持二进制
        const r = await cli.uploadFile(file, ${JSON.stringify(UPLOAD_DIR)})
        const hashBuffer = await crypto.subtle.digest('SHA-256', arr.buffer)
        const hash = Array.from(new Uint8Array(hashBuffer)).map((b) => b.toString(16).padStart(2, '0')).join('')
        return { ...r, sha256: hash }
      })()`,
    )
    console.log('  上传结果:', JSON.stringify(upResult))
    assert(upResult.sha256 === expected, '本端 sha256 一致')

    console.log('[4] render 落盘校验 ...')
    const remotePath = `${UPLOAD_DIR}/${UPLOAD_NAME}`
    const diskHash = sha256(await readFile(remotePath))
    assert(diskHash === expected, `落盘 sha256 一致 (${diskHash.slice(0, 16)}...)`)

    console.log('[5] 下载回来校验 ...')
    const down = await evaluate(`window.__ft.download(${JSON.stringify(remotePath)})`)
    assert(down.sha256 === expected && down.size === BIG_SIZE, `下载 sha256/大小一致 (${down.size} bytes)`)

    await rm(remotePath, { force: true })
    console.log('\n全部通过 ✔')
  } finally {
    await cleanup()
  }
}

main().catch((err) => {
  console.error('\n测试失败:', err)
  process.exit(1)
})
