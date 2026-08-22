// Direct Web Client regression test for CEF OSR PET_POPUP composition.
// A px_render WebView instance must already be listening at PX_POPUP_TEST_URL.
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const URL = process.env.PX_POPUP_TEST_URL ||
  'http://127.0.0.1:32997/web_client/?deviceId=popup-test&password=popup-pass'
const OUTPUT = process.env.PX_POPUP_TEST_SCREENSHOT ||
  path.join(os.tmpdir(), 'px-webview-popup-surface.png')
const CLOSED_OUTPUT = OUTPUT.replace(/(\.[^.]+)?$/, '-closed$1')
const PORT = Number(process.env.PX_POPUP_TEST_CDP_PORT || 9531)
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

const chrome = spawn(CHROME, [
  '--headless=new', `--remote-debugging-port=${PORT}`,
  `--user-data-dir=${path.join(os.tmpdir(), `px-popup-e2e-${Date.now()}`)}`,
  '--no-first-run', 'about:blank',
], { stdio: 'ignore' })

let ws
let commandId = 0
const pending = new Map()
const command = (method, params = {}) => new Promise((resolve, reject) => {
  const id = ++commandId
  pending.set(id, { resolve, reject })
  ws.send(JSON.stringify({ id, method, params }))
})
const evaluate = async (expression) => {
  const result = await command('Runtime.evaluate', {
    expression, returnByValue: true, awaitPromise: true, userGesture: true,
  })
  if (result.exceptionDetails) throw new Error(result.exceptionDetails.text || 'evaluation failed')
  return result.result?.value
}

const captureVideo = async (output) => {
  const dataUrl = await evaluate(`(() => {
    const video = document.querySelector('video')
    const canvas = document.createElement('canvas')
    canvas.width = video.videoWidth
    canvas.height = video.videoHeight
    canvas.getContext('2d').drawImage(video, 0, 0)
    return canvas.toDataURL('image/png')
  })()`)
  fs.writeFileSync(output, Buffer.from(dataUrl.replace(/^data:image\/png;base64,/, ''), 'base64'))
}

const popupProbe = () => evaluate(`(() => {
  const video = document.querySelector('video')
  if (!video || video.readyState < 2) return -1
  const canvas = document.createElement('canvas')
  canvas.width = video.videoWidth
  canvas.height = video.videoHeight
  const context = canvas.getContext('2d')
  context.drawImage(video, 0, 0)
  const pixel = context.getImageData(
    Math.floor(video.videoWidth * 0.27),
    Math.floor(video.videoHeight * 0.23),
    1, 1,
  ).data
  return pixel[0] + pixel[1] + pixel[2]
})()`)

async function main() {
  for (let retry = 0; retry < 60; retry += 1) {
    try {
      if ((await fetch(`http://127.0.0.1:${PORT}/json/version`)).ok) break
    } catch { /* retry */ }
    await sleep(250)
  }
  const target = await (await fetch(
    `http://127.0.0.1:${PORT}/json/new?${encodeURIComponent(URL)}`,
    { method: 'PUT' },
  )).json()
  ws = new WebSocket(target.webSocketDebuggerUrl)
  ws.onmessage = ({ data }) => {
    const message = JSON.parse(data)
    const waiter = pending.get(message.id)
    if (!waiter) return
    pending.delete(message.id)
    message.error ? waiter.reject(new Error(message.error.message)) : waiter.resolve(message.result)
  }
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject })
  await command('Runtime.enable')

  let inputConnected = false
  for (let retry = 0; retry < 120; retry += 1) {
    inputConnected = await evaluate(
      `Boolean(window.__input?.attached?.()) && window.__conn?.status?.() === 'connected'`,
    ).catch(() => false)
    if (inputConnected) break
    await sleep(250)
  }
  if (!inputConnected) throw new Error('WebView input channel did not connect')

  // The manual fixture can be completely static. If no video frame has been
  // requested yet, inject the select click through the real data channel; the
  // resulting PET_POPUP paint must itself produce the first composited frame.
  await evaluate(`window.__input.testSend({ x: 0.18, y: 0.16, keyCode: 'Unmapped' })`)

  let geometry
  for (let retry = 0; retry < 120; retry += 1) {
    geometry = await evaluate(`(() => {
      const video = document.querySelector('video')
      if (!video || video.readyState < 2) return null
      const rect = video.getBoundingClientRect()
      const scale = Math.min(rect.width / video.videoWidth, rect.height / video.videoHeight)
      const width = video.videoWidth * scale
      const height = video.videoHeight * scale
      return {
        x: rect.left + (rect.width - width) / 2,
        y: rect.top + (rect.height - height) / 2,
        width, height,
        videoWidth: video.videoWidth, videoHeight: video.videoHeight,
        connection: window.__conn?.status?.() || '',
      }
    })()`).catch(() => null)
    if (geometry?.connection === 'connected') break
    await sleep(250)
  }
  if (!geometry || geometry.connection !== 'connected') {
    throw new Error(`WebView did not connect: ${JSON.stringify(geometry)}`)
  }

  let popupVisible = false
  for (let retry = 0; retry < 120; retry += 1) {
    if (await popupProbe() > 600) {
      popupVisible = true
      break
    }
    await sleep(250)
  }
  if (!popupVisible) throw new Error('popup surface did not reach the remote video')

  await captureVideo(OUTPUT)

  // Select the last item through a real browser pointer event. This exercises
  // the Web Client coordinate mapping and CEF popup hit-testing together.
  const optionX = geometry.x + geometry.width * 0.18
  const optionY = geometry.y + geometry.height * 0.36
  await command('Input.dispatchMouseEvent', {
    type: 'mousePressed', x: optionX, y: optionY,
    button: 'left', buttons: 1, clickCount: 1,
  })
  await command('Input.dispatchMouseEvent', {
    type: 'mouseReleased', x: optionX, y: optionY,
    button: 'left', buttons: 0, clickCount: 1,
  })
  let popupClosed = false
  for (let retry = 0; retry < 120; retry += 1) {
    if (await popupProbe() < 300) {
      popupClosed = true
      break
    }
    await sleep(250)
  }
  if (!popupClosed) throw new Error('popup surface did not clear from the remote video')
  await captureVideo(CLOSED_OUTPUT)

  const popupHidden = fs.readFileSync(OUTPUT).compare(fs.readFileSync(CLOSED_OUTPUT)) !== 0
  if (!popupHidden) throw new Error('popup did not close after selecting an item')
  console.log(`PASS popup open/selection/close ${geometry.videoWidth}x${geometry.videoHeight}: ${OUTPUT}, ${CLOSED_OUTPUT}`)
}

main()
  .catch((error) => { console.error(`FAIL ${error.message}`); process.exitCode = 1 })
  .finally(() => {
    try { ws?.close() } catch { /* ignore */ }
    try { chrome.kill() } catch { /* ignore */ }
  })
