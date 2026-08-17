// CDP 无头 Chrome 验证 px_web_client 手柄回传(Gamepad API -> kGamepadState -> render ViGEm 虚拟 X360)
// 用法: node scripts/cdp_gamepad_test.mjs
// 依赖: 无(Node 22 内置 fetch/WebSocket),Chrome + Pixels 套件已在运行
// 验证点:
//   1. 工具条「手柄」按钮开启 -> __gamepad.on() === true
//   2. 开启后 render 日志出现 joystick 插件分配虚拟手柄(Connect VIGEM / target connected)
//   3. testSend 注入状态 -> XInputGetState 在虚拟 X360 上读到对应按键/摇杆/扳机
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
// joystick 插件有独立日志(joystick.dll.log),分配虚拟手柄的记录在这里
const RENDER_LOG = 'C:/Users/Public/Pixels/px_logs/joystick.dll.log'
const PAGE_URL =
  'http://127.0.0.1:20371/web_client/?deviceId=600378210&streamId=gamepad1&pwd_md5=698d51a19d8a121ce581499d7b701668'
const CDP_PORT = 9223

// 测试注入的 XInput 状态:A(0x1000) + LT=200 + LX=32767 + LY=-32768 + RY=12345
const TEST_STATE = {
  buttons: 0x1000,
  leftTrigger: 200,
  rightTrigger: 0,
  thumbLx: 32767,
  thumbLy: -32768,
  thumbRx: 0,
  thumbRy: 12345,
}

const results = []
function report(name, ok, detail = '') {
  results.push({ name, ok, detail })
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  -- ' + detail : ''}`)
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

// ---------- Chrome 启动 ----------
const profile = path.join(os.tmpdir(), `cdp-gamepad-${Date.now()}`)
const chrome = spawn(
  CHROME,
  [
    '--headless=new',
    `--remote-debugging-port=${CDP_PORT}`,
    `--user-data-dir=${profile}`,
    '--no-first-run',
    '--autoplay-policy=no-user-gesture-required',
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

// 读取本机 XInput 控制器状态(PowerShell 调 xinput1_4.dll)
async function queryXInput() {
  const ps = `
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class XI {
  [StructLayout(LayoutKind.Sequential)]
  public struct GP { public ushort wButtons; public byte bLeftTrigger; public byte bRightTrigger;
    public short sThumbLX; public short sThumbLY; public short sThumbRX; public short sThumbRY; }
  [StructLayout(LayoutKind.Sequential)]
  public struct STATE { public uint dwPacketNumber; public GP Gamepad; }
  [DllImport("xinput1_4.dll")] public static extern uint XInputGetState(uint u, out STATE s);
}
'@
$out = @()
for ($i = 0; $i -lt 4; $i++) {
  $s = New-Object XI+STATE
  $r = [XI]::XInputGetState($i, [ref]$s)
  if ($r -eq 0) {
    $g = $s.Gamepad
    $out += ("{0},{1},{2},{3},{4},{5},{6},{7}" -f $i, $g.wButtons, $g.bLeftTrigger, $g.bRightTrigger, $g.sThumbLX, $g.sThumbLY, $g.sThumbRX, $g.sThumbRY)
  }
}
$out -join ";"
`
  return new Promise((resolve) => {
    const p = spawn('powershell', ['-NoProfile', '-Command', ps], { stdio: ['ignore', 'pipe', 'ignore'] })
    let out = ''
    p.stdout.on('data', (d) => (out += d.toString()))
    p.on('close', () => {
      const pads = out
        .trim()
        .split(';')
        .filter(Boolean)
        .map((line) => {
          const [index, buttons, lt, rt, lx, ly, rx, ry] = line.split(',').map(Number)
          return { index, buttons, lt, rt, lx, ly, rx, ry }
        })
      resolve(pads)
    })
  })
}

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

  // 等待 WebRTC 连接建立
  let connected = false
  for (let i = 0; i < 80; i++) {
    const s = await evaluate(`window.__conn?.status?.() ?? ''`).catch(() => '')
    if (s === 'connected') { connected = true; break }
    await sleep(500)
  }
  report('WebRTC 自动连接', connected)
  if (!connected) {
    const logs = await evaluate(`document.querySelector('.log-panel')?.textContent?.slice(-800) ?? ''`).catch(() => '')
    console.log('页面日志尾部:', logs)
    throw new Error('连接失败,中止')
  }

  // 1. 通过 __gamepad 钩子开启手柄(与点「手柄」按钮同一入口)
  const logOff = fs.statSync(RENDER_LOG).size
  await evaluate(`window.__gamepad.toggle()`)
  await sleep(300)
  const gpOn = await evaluate(`window.__gamepad.on()`)
  report('手柄开关开启', gpOn === true)

  // 2. render 应收到 kHello{enable_controller} 并分配虚拟手柄
  await sleep(1500)
  const helloLog = readNewLog(logOff)
  const allocOk = /Connect VIGEM success|target connected: 1/i.test(helloLog)
  const allocFail = /Connect VIGEM failed|Alloc controller failed/i.test(helloLog)
  report('render 分配虚拟手柄', allocOk && !allocFail,
    allocOk ? '日志命中 ViGEM 连接成功' : (allocFail ? '日志显示 ViGEM 失败(可能未装 ViGEmBus 驱动)' : '日志无 joystick 记录'))

  // 3. 注入状态 -> 连发保持(游戏通常持续读态),XInputGetState 应读到
  //    render 侧 ViGEm 只在收到消息时更新,这里周期性 testSend 模拟持续输入
  const basePads = await queryXInput()
  console.log('注入前 XInput 控制器:', JSON.stringify(basePads))
  const sendExpr = `window.__gamepad.testSend(${JSON.stringify(TEST_STATE)})`
  // 连续发 3s(每 100ms 一帧),保证采样窗口内虚拟手柄处于该状态
  const sender = setInterval(() => { void evaluate(sendExpr).catch(() => {}) }, 100)
  await sleep(1200)
  const pads = await queryXInput()
  clearInterval(sender)
  await evaluate(sendExpr).catch(() => {})
  console.log('注入后 XInput 控制器:', JSON.stringify(pads))

  const hit = pads.find(
    (p) =>
      (p.buttons & 0x1000) !== 0 &&
      p.lt === TEST_STATE.leftTrigger &&
      p.lx === TEST_STATE.thumbLx &&
      p.ly === TEST_STATE.thumbLy &&
      p.ry === TEST_STATE.thumbRy,
  )
  report('虚拟手柄状态回放(XInputGetState)', !!hit,
    hit ? `控制器 #${hit.index}: buttons=0x${hit.buttons.toString(16)} LT=${hit.lt} LX=${hit.lx} LY=${hit.ly} RY=${hit.ry}`
      : '未在 XInput 控制器上读到注入状态')

  // 4. 释放 + 关闭
  await evaluate(`window.__gamepad.testSend(${JSON.stringify({ buttons: 0, leftTrigger: 0, rightTrigger: 0, thumbLx: 0, thumbLy: 0, thumbRx: 0, thumbRy: 0 })})`)
  await evaluate(`window.__gamepad.toggle()`)
  await sleep(300)
  const gpOff = await evaluate(`window.__gamepad.on()`)
  report('手柄开关关闭', gpOff === false)

  const failed = results.filter((x) => !x.ok)
  console.log(`\n==== ${results.length - failed.length}/${results.length} 通过 ====`)
  process.exit(failed.length ? 1 : 0)
}

main().catch((e) => {
  console.error('测试中止:', e.message)
  process.exit(2)
})
