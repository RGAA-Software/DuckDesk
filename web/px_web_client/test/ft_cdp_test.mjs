// 无头 Chrome CDP 驱动:验证 web_client 文件传输(列目录/上传/下载)
// 用法: node test/ft_cdp_test.mjs
// 前提: GoDesk 套件已启动(render 在 127.0.0.1:20371),dist 已同步到 build_official/dist/web_client
import { spawn } from 'node:child_process'
import { createHash } from 'node:crypto'
import { readFile, rm } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const CDP_PORT = 9222
const PAGE_URL =
  'http://127.0.0.1:20371/web_client/?deviceId=600378210&streamId=ft1&pwd_md5=81dc9bdb52d04dc20036dbd8313ed055'
const UPLOAD_DIR = 'C:/Users/Public'
const UPLOAD_NAME = `ft_web_test_${Date.now()}.txt`

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const sha256 = (buf) => createHash('sha256').update(buf).digest('hex')

function assert(cond, msg) {
  if (!cond) throw new Error(`断言失败: ${msg}`)
  console.log(`  OK: ${msg}`)
}

// ---------- CDP 客户端 ----------
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
  const result = await cdpSend('Runtime.evaluate', {
    expression,
    awaitPromise: true,
    returnByValue: true,
  })
  if (result.exceptionDetails) {
    throw new Error(`页面内执行出错: ${JSON.stringify(result.exceptionDetails.exception?.description ?? result.exceptionDetails.text)}`)
  }
  return result.result?.value
}

async function waitFor(expr, timeoutMs, desc) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    if (await evaluate(expr)) return
    await sleep(1000)
  }
  throw new Error(`等待超时: ${desc}`)
}

// ---------- 主流程 ----------
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
    // 等 CDP 起来
    let version = null
    for (let i = 0; i < 30; i++) {
      try {
        version = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json()
        break
      } catch {
        await sleep(500)
      }
    }
    if (!version) throw new Error('CDP 端口未就绪')
    console.log('Chrome:', version.Browser)

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

    console.log('\n[1] 等待页面连接 + ft_data_channel 就绪 ...')
    await waitFor('!!(window.__ft && window.__ft.ready())', 60000, 'window.__ft.ready()')
    console.log('  OK: ft 通道就绪')

    console.log('\n[2] 列盘符 listDir("/") ...')
    const disks = await evaluate('window.__ft.listDir("/")')
    console.log('  盘符:', disks.files.map((f) => f.path).join(', '))
    assert(disks.files.length > 0, '返回盘符列表非空')

    console.log(`\n[3] 列真实目录 listDir("${UPLOAD_DIR}") ...`)
    const dirList = await evaluate(`window.__ft.listDir(${JSON.stringify(UPLOAD_DIR)})`)
    console.log(`  条目数: ${dirList.files.length},前 5 个:`,
      dirList.files.slice(0, 5).map((f) => `${f.type === 2 ? 'F' : 'D'}:${f.name}`).join(', '))
    assert(dirList.files.length > 0, '目录内容非空')

    console.log('\n[4] 上传文本文件 ...')
    const content = `GoDesk web_client ft upload test\n时间戳: ${new Date().toISOString()}\n随机: ${Math.random()}\n中文内容校验: 远程桌面文件传输\n`
    const expectedHash = sha256(Buffer.from(content, 'utf8'))
    const upResult = await evaluate(
      `window.__ft.uploadText(${JSON.stringify(UPLOAD_NAME)}, ${JSON.stringify(UPLOAD_DIR)}, ${JSON.stringify(content)})`,
    )
    console.log('  上传结果:', JSON.stringify(upResult))
    assert(upResult.sha256 === expectedHash, `本端 sha256 一致 (${expectedHash.slice(0, 16)}...)`)
    // upload() 的 Promise 只有在收到 render 的 kFileTransRespUpload(res=true) 后才 resolve
    assert(upResult.taskId.startsWith('up-'), `render 确认上传成功 (taskId=${upResult.taskId})`)

    console.log('\n[5] 在 render 机器上校验落盘文件 ...')
    const remotePath = `${UPLOAD_DIR}/${UPLOAD_NAME}`
    const diskBytes = await readFile(remotePath)
    const diskHash = sha256(diskBytes)
    assert(diskHash === expectedHash, `落盘文件 sha256 一致 (${diskHash.slice(0, 16)}...)`)
    assert(diskBytes.toString('utf8') === content, '落盘文件内容逐字节一致')

    console.log('\n[6] 下载该文件回来 ...')
    const downResult = await evaluate(`window.__ft.download(${JSON.stringify(remotePath)})`)
    console.log('  下载结果:', JSON.stringify(downResult))
    assert(downResult.sha256 === expectedHash, `下载文件 sha256 一致 (${downResult.sha256.slice(0, 16)}...)`)
    assert(downResult.size === Buffer.byteLength(content, 'utf8'), `大小一致 (${downResult.size} bytes)`)

    console.log('\n[7] 清理远端测试文件 ...')
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
