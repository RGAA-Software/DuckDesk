// 无头 Chrome CDP 冒烟:验证 web_client 新文件传输(rustdesk 协议,阶段 4)
// 流程: 连接 -> 版本门控 -> 列目录 -> 上传小文件 -> 下载回校验 sha256 -> 删除远端文件
// 用法:
//   node test/ft_cdp_test.mjs                       # 默认对 10.0.0.90 冒烟
//   环境变量覆盖:
//     FT_TARGET_BASE  被控 render 基址(默认 http://10.0.0.90:20371)
//     FT_DEVICE_ID    目标 deviceId(默认 001190520,/get/render/configuration 可查)
//     FT_PWD_MD5      安全密码 md5(未设密码可留空)
//     FT_DIR          远端测试目录(默认 C:/Users/Public)
// 前提: 本仓库已 npm run build(脚本用本地静态服务挂 dist,并把 /alloc、/get 代理到目标机,
//       这样被控不需要部署新版 web_client 也能冒烟新代码)
import { spawn } from 'node:child_process'
import { createHash } from 'node:crypto'
import { createServer } from 'node:http'
import { readFile } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const CDP_PORT = 9225
const TARGET_BASE = process.env.FT_TARGET_BASE || 'http://10.0.0.90:20371'
const DEVICE_ID = process.env.FT_DEVICE_ID || '001190520'
const PWD_MD5 = process.env.FT_PWD_MD5 || ''
const STREAM_ID = process.env.FT_STREAM_ID || `ft${Date.now() % 100000}`
const REMOTE_DIR = process.env.FT_DIR || 'C:/Users/Public'
const UPLOAD_NAME = `ft_web_smoke_${Date.now()}.txt`
const DIST = path.join(import.meta.dirname, '../dist')

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const sha256 = (buf) => createHash('sha256').update(buf).digest('hex')

function assert(cond, msg) {
  if (!cond) throw new Error(`断言失败: ${msg}`)
  console.log(`  OK: ${msg}`)
}

// ---------- 本地静态服务 + 信令代理 ----------
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.png': 'image/png', '.svg': 'image/svg+xml' }

async function startLocalServer() {
  const server = createServer((req, res) => {
    const url = new URL(req.url || '/', 'http://x')
    if (url.pathname.startsWith('/alloc') || url.pathname.startsWith('/get')) {
      // 代理到被控 render(透传 query 与 body)
      const target = `${TARGET_BASE}${url.pathname}${url.search}`
      const chunks = []
      req.on('data', (c) => chunks.push(c))
      req.on('end', () => {
        fetch(target, {
          method: req.method,
          headers: { 'content-type': req.headers['content-type'] || 'application/json' },
          body: req.method === 'GET' ? undefined : Buffer.concat(chunks),
        })
          .then(async (r) => {
            res.writeHead(r.status, { 'content-type': r.headers.get('content-type') || 'application/json' })
            res.end(Buffer.from(await r.arrayBuffer()))
          })
          .catch((err) => {
            res.writeHead(502)
            res.end(String(err))
          })
      })
      return
    }
    // 静态:dist
    let p = path.join(DIST, url.pathname === '/' ? 'index.html' : decodeURIComponent(url.pathname))
    if (!p.startsWith(path.resolve(DIST))) {
      res.writeHead(403)
      res.end()
      return
    }
    readFile(p)
      .then((data) => {
        res.writeHead(200, { 'content-type': MIME[path.extname(p)] || 'application/octet-stream' })
        res.end(data)
      })
      .catch(() => {
        res.writeHead(404)
        res.end('not found')
      })
  })
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
  return { server, port: server.address().port }
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

// 轮询作业终态(done/error/cancelled)
async function waitJob(jobId, timeoutMs) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    const jobs = await evaluate('window.__ft.jobs()')
    const job = (jobs || []).find((j) => j.id === jobId)
    if (job && job.state !== 'running' && job.state !== 'pending') return job
    await sleep(500)
  }
  throw new Error(`等待作业终态超时: ${jobId}`)
}

// ---------- 主流程 ----------
async function main() {
  // 先探测目标可达性,不可达直接报告跳过
  let cfg = null
  try {
    cfg = await (await fetch(`${TARGET_BASE}/get/render/configuration`, { signal: AbortSignal.timeout(4000) })).json()
  } catch (err) {
    console.log(`SKIP: 目标 ${TARGET_BASE} 不可达 (${err instanceof Error ? err.message : err}),冒烟未执行`)
    process.exit(2)
  }
  console.log(`目标: ${TARGET_BASE} device_id=${cfg?.data?.device_id} app=${cfg?.data?.app_version}`)
  if (cfg?.data?.device_id && cfg.data.device_id !== DEVICE_ID) {
    console.log(`  注意: 配置的 deviceId(${DEVICE_ID})与目标实际(${cfg.data.device_id})不同,以实际为准`)
  }
  const deviceId = cfg?.data?.device_id || DEVICE_ID

  // 预检信令鉴权:dummy SDP 探针,700 = 安全密码不对,避免白等 90s 连接超时
  try {
    const probe = await (
      await fetch(
        `${TARGET_BASE}/alloc/local/rtc?device_id=${deviceId}&stream_id=ft_probe&safety_pwd_md5=${encodeURIComponent(PWD_MD5)}`,
        { method: 'POST', headers: { 'content-type': 'application/json' }, body: '{"sdp":"probe"}', signal: AbortSignal.timeout(Number(process.env.FT_PROBE_TIMEOUT_MS || 25000)) },
      )
    ).json()
    if (probe?.code === 700) {
      console.log(`SKIP: 安全密码校验失败(700)。请用 FT_PWD_MD5=<md5(安全密码)> 重试;冒烟未执行`)
      process.exit(2)
    }
    console.log(`  信令鉴权预检: code=${probe?.code}(${probe?.code === 700 ? '密码错' : '通过/进入 SDP 流程'})`)
  } catch (err) {
    console.log(`SKIP: 信令预检失败 (${err instanceof Error ? err.message : err});冒烟未执行`)
    process.exit(2)
  }

  const { server, port } = await startLocalServer()
  const pageUrl = `http://127.0.0.1:${port}/?deviceId=${deviceId}&streamId=${STREAM_ID}&pwd_md5=${PWD_MD5}`
  console.log(`页面: ${pageUrl}`)

  const userDataDir = path.join(os.tmpdir(), `ft_cdp_chrome_${Date.now()}`)
  const chrome = spawn(CHROME, [
    '--headless=new',
    `--remote-debugging-port=${CDP_PORT}`,
    `--user-data-dir=${userDataDir}`,
    '--no-first-run',
    '--disable-gpu',
    '--autoplay-policy=no-user-gesture-required',
    'about:blank',
  ])
  const cleanup = async () => {
    try { chrome.kill() } catch { /* ignore */ }
    server.close()
    await import('node:fs/promises').then((fs) => fs.rm(userDataDir, { recursive: true, force: true }).catch(() => {}))
  }

  const remotePath = `${REMOTE_DIR}/${UPLOAD_NAME}`
  try {
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
      await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(pageUrl)}`, { method: 'PUT' })
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
    await waitFor('!!(window.__ft && window.__ft.ready())', 90000, 'window.__ft.ready()')
    console.log('  OK: ft 通道就绪')

    console.log('\n[2] 版本门控: __ft.supported() ...')
    const supported = await evaluate('window.__ft.supported()')
    if (!supported) {
      console.log('SKIP: 对端 FT 协议版本不是 2(旧版被控),入口按设计置灰,后续步骤不适用')
      process.exit(2)
    }
    console.log('  OK: ftProtocolVersion = 2')

    console.log('\n[3] 列盘符 listDir("/") ...')
    const disks = await evaluate('window.__ft.listDir("/")')
    console.log('  盘符:', disks.files.map((f) => f.name).join(', '))
    assert(disks.files.length > 0, '盘符列表非空')

    console.log(`\n[4] 列目录 listDir("${REMOTE_DIR}") ...`)
    const dirList = await evaluate(`window.__ft.listDir(${JSON.stringify(REMOTE_DIR)})`)
    console.log(`  条目数: ${dirList.files.length}`)
    assert(dirList.files.length >= 0 && dirList.path, `目录回包正常 (path=${dirList.path})`)

    console.log('\n[5] 上传小文本文件 ...')
    const content = `px web_client ft smoke\n时间戳: ${new Date().toISOString()}\n随机: ${Math.random()}\n中文内容校验: 远程桌面文件传输\n`
    const expectedHash = sha256(Buffer.from(content, 'utf8'))
    const up = await evaluate(
      `window.__ft.uploadText(${JSON.stringify(UPLOAD_NAME)}, ${JSON.stringify(REMOTE_DIR)}, ${JSON.stringify(content)})`,
    )
    console.log('  已投递上传作业:', JSON.stringify(up))
    assert(up.sha256 === expectedHash, `本端 sha256 一致 (${expectedHash.slice(0, 16)}...)`)
    const upJob = await waitJob(up.jobId, 60000)
    assert(upJob.state === 'done', `上传作业完成 (state=${upJob.state}${upJob.error ? ', ' + upJob.error : ''})`)

    console.log('\n[6] 列目录确认远端文件存在 ...')
    const dir2 = await evaluate(`window.__ft.listDir(${JSON.stringify(REMOTE_DIR)})`)
    const found = dir2.files.find((f) => f.name === UPLOAD_NAME)
    assert(!!found, `远端目录出现 ${UPLOAD_NAME}`)
    assert(found.size === Buffer.byteLength(content, 'utf8'), `远端文件大小一致 (${found.size})`)

    console.log('\n[7] 下载该文件回来(内存 sink)并比对 sha256 ...')
    const down = await evaluate(`window.__ft.download(${JSON.stringify(remotePath)})`)
    console.log('  下载结果:', JSON.stringify({ name: down.name, size: down.size, sha256: down.sha256 }))
    assert(down.sha256 === expectedHash, `下载 sha256 一致 (${down.sha256.slice(0, 16)}...)`)
    assert(down.size === Buffer.byteLength(content, 'utf8'), `大小一致 (${down.size} bytes)`)

    console.log('\n[8] 删除远端测试文件 ...')
    await evaluate(`window.__ft.removeFile(${JSON.stringify(remotePath)})`)
    const dir3 = await evaluate(`window.__ft.listDir(${JSON.stringify(REMOTE_DIR)})`)
    assert(!dir3.files.find((f) => f.name === UPLOAD_NAME), '远端文件已删除')

    console.log('\n全部通过 ✔')
  } finally {
    await cleanup()
  }
}

main().catch((err) => {
  console.error('\n测试失败:', err)
  process.exit(1)
})
