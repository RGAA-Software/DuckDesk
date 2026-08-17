// Start desktop render on a remote px_service via /service/message ws.
// Usage: node scripts/service_start_render.mjs [host] [port]
import net from 'node:net'
import crypto from 'node:crypto'

const HOST = process.argv[2] || '10.0.0.70'
const PORT = Number(process.argv[3] || 20375)

// ---- protobuf hand-encoding ----
// ServiceMessage { type=kSrvStartServer(0, omitted) ; MsgStartServer start_server = 2 }
// MsgStartServer { string work_dir = 1; string app_path = 2; repeated string args = 3 }
function fieldStr(num, s) {
  const b = Buffer.from(s, 'utf8')
  return Buffer.concat([Buffer.from([(num << 3) | 2]), varint(b.length), b])
}
function varint(n) {
  const out = []
  while (n > 0x7f) { out.push((n & 0x7f) | 0x80); n >>>= 7 }
  out.push(n)
  return Buffer.from(out)
}
const start = Buffer.concat([
  fieldStr(1, 'C:/Program Files/PixelsRender'),
  fieldStr(2, 'C:/Program Files/PixelsRender/px_render.exe'),
  fieldStr(3, '--app_mode=desktop'),
])
const msg = Buffer.concat([Buffer.from([(2 << 3) | 2]), varint(start.length), start])

// ---- minimal ws client (binary, masked) ----
const key = crypto.randomBytes(16).toString('base64')
const sock = net.connect(PORT, HOST, () => {
  sock.write(
    `GET /service/message HTTP/1.1\r\nHost: ${HOST}:${PORT}\r\nUpgrade: websocket\r\n` +
    `Connection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n` +
    `Authorization: websocket-client-authorization\r\n\r\n`)
})
let upgraded = false
let handshake = Buffer.alloc(0)
sock.on('data', (chunk) => {
  if (!upgraded) {
    handshake = Buffer.concat([handshake, chunk])
    const idx = handshake.indexOf('\r\n\r\n')
    if (idx === -1) return
    const head = handshake.slice(0, idx).toString()
    if (!head.includes('101')) {
      console.error('upgrade failed:', head.split('\r\n')[0])
      process.exit(1)
    }
    upgraded = true
    // send one binary frame, masked
    const mask = crypto.randomBytes(4)
    const masked = Buffer.from(msg)
    for (let i = 0; i < masked.length; i++) masked[i] ^= mask[i % 4]
    let header
    if (msg.length < 126) {
      header = Buffer.from([0x82, 0x80 | msg.length])
    } else {
      header = Buffer.alloc(4)
      header[0] = 0x82; header[1] = 0x80 | 126
      header.writeUInt16BE(msg.length, 2)
    }
    sock.write(Buffer.concat([header, mask, masked]))
    console.log(`StartServer sent to ${HOST}:${PORT} (desktop render)`)
    setTimeout(() => process.exit(0), 500)
  }
})
sock.on('error', (e) => { console.error('socket error:', e.message); process.exit(1) })
setTimeout(() => { console.error('timeout'); process.exit(1) }, 8000)
