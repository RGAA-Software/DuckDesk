/**
 * Inject MsgAuthInfo into px_service local WS (/service/message)
 * so cms_client_loop can connect to CMS.
 *
 * Usage:
 *   node scripts/inject_service_auth.mjs --host 127.0.0.1 --port 20375 \
 *     --device-id debug-svc-1 --appkey XXX --cms-host 127.0.0.1 --cms-port 30500
 */
import net from 'node:net'
import crypto from 'node:crypto'

function parseArgs(argv) {
  const out = {
    host: '127.0.0.1',
    port: 20375,
    deviceId: 'debug-svc-1',
    appkey: '',
    cmsHost: '127.0.0.1',
    cmsPort: 30500,
    cmsSsl: true,
  }
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i]
    const n = argv[i + 1]
    if (a === '--host') out.host = n
    else if (a === '--port') out.port = Number(n)
    else if (a === '--device-id') out.deviceId = n
    else if (a === '--appkey') out.appkey = n
    else if (a === '--cms-host') out.cmsHost = n
    else if (a === '--cms-port') out.cmsPort = Number(n)
    else if (a === '--cms-ssl') out.cmsSsl = n !== 'false'
    else continue
    i++
  }
  if (!out.appkey) throw new Error('--appkey required')
  return out
}

function encVarint(n) {
  const bytes = []
  let x = n >>> 0
  while (x >= 0x80) {
    bytes.push((x & 0x7f) | 0x80)
    x >>>= 7
  }
  bytes.push(x)
  return Buffer.from(bytes)
}

function encKey(field, wire) {
  return encVarint((field << 3) | wire)
}

function encString(field, s) {
  const b = Buffer.from(s, 'utf8')
  return Buffer.concat([encKey(field, 2), encVarint(b.length), b])
}

function encInt32(field, n) {
  return Buffer.concat([encKey(field, 0), encVarint(n)])
}

function encInt64(field, n) {
  // varint for positive int64
  const bytes = []
  let x = BigInt(n)
  while (x >= 0x80n) {
    bytes.push(Number(x & 0x7fn) | 0x80)
    x >>= 7n
  }
  bytes.push(Number(x))
  return Buffer.concat([encKey(field, 0), Buffer.from(bytes)])
}

function encLen(field, payload) {
  return Buffer.concat([encKey(field, 2), encVarint(payload.length), payload])
}

function encodeAuthInfo(opts) {
  // MsgAuthInfo fields from px_service_message.proto
  return Buffer.concat([
    encString(1, opts.deviceId),
    encString(2, 'e2e-auth'),
    encString(3, 'e2e'),
    encString(4, 'e2e-mc'),
    encString(5, opts.appkey),
    encInt32(6, 2),
    encInt32(7, 365),
    encInt32(8, 16),
    encInt64(9, BigInt(Date.now()) + 86400000n * 30n),
    encString(10, opts.cmsHost),
    encInt32(11, opts.cmsPort),
    // bool cms_ssl = 12 (proto3 default is false = plain ws, encode it explicitly)
    encInt32(12, opts.cmsSsl ? 1 : 0),
  ])
}

function encodeServiceAuthMessage(opts) {
  // ServiceMessageType::kSrvAuthInfo — check enum; typically last values.
  // From generated: KSrvAuthInfo. In tests: ServiceMessageType::AuthInfo as i32
  // Enum in proto:
  // kSrvStartServer=0 ... need exact. Looking at ServiceMessageType in rust:
  // AuthInfo = KSrvAuthInfo
  const auth = encodeAuthInfo(opts)
  // We'll discover type value from a known encode if needed; common pattern
  // in this repo tests uses ServiceMessageType::AuthInfo as i32.
  // From px_service_message.proto enum order:
  // kSrvStartServer = 0;
  // kSrvStopServer = 1;
  // kSrvRestartServer = 2;
  // kSrvHeartBeat = 3;
  // kSrvHeartBeatResp = 4;
  // kSrvReqCtrlAltDelete = 5;
  // kSrvAuthInfo = 6;
  const typeAuthInfo = 6
  return Buffer.concat([encInt32(1, typeAuthInfo), encLen(8, auth)])
}

function wsAcceptKey(key) {
  return crypto
    .createHash('sha1')
    .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
    .digest('base64')
}

function maskFrame(opcode, payload) {
  const mask = crypto.randomBytes(4)
  const masked = Buffer.alloc(payload.length)
  for (let i = 0; i < payload.length; i++) masked[i] = payload[i] ^ mask[i % 4]
  let header
  if (payload.length < 126) {
    header = Buffer.alloc(2)
    header[0] = 0x80 | opcode
    header[1] = 0x80 | payload.length
  } else if (payload.length < 65536) {
    header = Buffer.alloc(4)
    header[0] = 0x80 | opcode
    header[1] = 0x80 | 126
    header.writeUInt16BE(payload.length, 2)
  } else {
    header = Buffer.alloc(10)
    header[0] = 0x80 | opcode
    header[1] = 0x80 | 127
    header.writeBigUInt64BE(BigInt(payload.length), 2)
  }
  return Buffer.concat([header, mask, masked])
}

async function main() {
  const opts = parseArgs(process.argv)
  const payload = encodeServiceAuthMessage(opts)
  const key = crypto.randomBytes(16).toString('base64')
  const req =
    `GET /service/message HTTP/1.1\r\n` +
    `Host: ${opts.host}:${opts.port}\r\n` +
    `Upgrade: websocket\r\n` +
    `Connection: Upgrade\r\n` +
    `Sec-WebSocket-Key: ${key}\r\n` +
    `Sec-WebSocket-Version: 13\r\n\r\n`

  await new Promise((resolve, reject) => {
    const sock = net.connect(opts.port, opts.host, () => {
      sock.write(req)
    })
    let buf = Buffer.alloc(0)
    let upgraded = false
    sock.on('data', (chunk) => {
      buf = Buffer.concat([buf, chunk])
      if (!upgraded) {
        const idx = buf.indexOf('\r\n\r\n')
        if (idx < 0) return
        const head = buf.subarray(0, idx).toString('utf8')
        if (!head.includes('101')) {
          reject(new Error('WS upgrade failed: ' + head.split('\r\n')[0]))
          sock.destroy()
          return
        }
        const expected = wsAcceptKey(key)
        if (!head.includes(expected)) {
          // still proceed; some servers omit exact check path
        }
        upgraded = true
        sock.write(maskFrame(0x2, payload)) // binary
        setTimeout(() => {
          sock.end()
          console.log(
            JSON.stringify({
              ok: true,
              deviceId: opts.deviceId,
              appkey: opts.appkey,
              cms: `${opts.cmsHost}:${opts.cmsPort}`,
              bytes: payload.length,
            }),
          )
          resolve()
        }, 300)
      }
    })
    sock.on('error', reject)
  })
}

main().catch((e) => {
  console.error(e)
  process.exit(1)
})
