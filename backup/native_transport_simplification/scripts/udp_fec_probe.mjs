// UDP FEC probe: bind to render's udp media channel, count data/parity shards.
// Usage: node scripts/udp_fec_probe.mjs [host] [port] [seconds]
import dgram from 'node:dgram'

const HOST = process.argv[2] || '10.0.0.70'
const PORT = Number(process.argv[3] || 20381)
const SECONDS = Number(process.argv[4] || 15)
const IDR_RATE = Number(process.env.IDR_RATE || 0) // simulated lost frames per second
const SEND_FS = process.env.SEND_FS === '1' // send FRAME_STATUS for fully received frames

const MAGIC = 0x4755 // 'GU'
const PKT_VIDEO = 1, PKT_CTRL = 3
const CTRL_HELLO = 1, CTRL_HEARTBEAT = 2
const CTRL_IDR = 3, CTRL_FRAME_STATUS = 4
const FLAG_PARITY = 0x8

function buildCtrl(subtype, s1, s2) {
  const a = Buffer.from(s1 || ''), b = Buffer.from(s2 || '')
  const len = 4 + 1 + (s1 != null ? 1 + a.length : 0) + (s2 != null ? 1 + b.length : 0)
  const buf = Buffer.alloc(len)
  buf.writeUInt16LE(MAGIC, 0); buf[2] = 1; buf[3] = PKT_CTRL
  let o = 4
  buf[o++] = subtype
  if (s1 != null) { buf[o++] = a.length; a.copy(buf, o); o += a.length }
  if (s2 != null) { buf[o++] = b.length; b.copy(buf, o); o += b.length }
  return buf
}

function buildFrameStatus(frameIndex, received, lost) {
  const buf = Buffer.alloc(4 + 1 + 8)
  buf.writeUInt16LE(MAGIC, 0); buf[2] = 1; buf[3] = PKT_CTRL
  buf[4] = CTRL_FRAME_STATUS
  buf.writeUInt32LE(frameIndex >>> 0, 5)
  buf.writeUInt16LE(received, 9)
  buf.writeUInt16LE(lost, 11)
  return buf
}

const sock = dgram.createSocket('udp4')
const frames = new Map() // frame_index -> {data, parity, dataShards, parityShards}
let videoPkts = 0, ctrlPkts = 0, otherPkts = 0

sock.on('message', (msg) => {
  if (msg.length < 24 || msg.readUInt16LE(0) !== MAGIC) { otherPkts++; return }
  const type = msg[3]
  if (type === PKT_CTRL) { ctrlPkts++; return }
  if (type !== PKT_VIDEO) { otherPkts++; return }
  videoPkts++
  const frameIndex = msg.readUInt32LE(4)
  const flags = msg[12]
  const dataShards = msg.readUInt16LE(14)
  const parityShards = msg.readUInt16LE(16)
  const shardIndex = msg.readUInt16LE(18)
  let f = frames.get(frameIndex)
  if (!f) { f = { data: 0, parity: 0, dataShards, parityShards, idx: new Set(), fsSent: false }; frames.set(frameIndex, f) }
  if (flags & FLAG_PARITY || shardIndex >= dataShards) f.parity++
  else f.data++
  f.idx.add(shardIndex)
  f.dataShards = dataShards; f.parityShards = parityShards
  if (SEND_FS && !f.fsSent && f.idx.size === f.dataShards + f.parityShards) {
    f.fsSent = true
    sock.send(buildFrameStatus(frameIndex, f.dataShards, 0), PORT, HOST)
  }
})

sock.bind(0, () => {
  const dev = 'fec-probe-dev', sid = 'fec-probe-stream-1'
  sock.send(buildCtrl(CTRL_HELLO, dev, sid), PORT, HOST)
  console.log(`hello sent to ${HOST}:${PORT}, listening ${SECONDS}s ...`)
  const hb = setInterval(() => sock.send(buildCtrl(CTRL_HEARTBEAT, sid, null), PORT, HOST), 1000)
  let idrTimer = null
  if (IDR_RATE > 0) {
    idrTimer = setInterval(() => sock.send(buildCtrl(CTRL_IDR, '', null), PORT, HOST), 1000 / IDR_RATE)
    console.log(`simulating ${IDR_RATE} lost frames/s (IDR requests)`)
  }
  setTimeout(() => {
    clearInterval(hb)
    if (idrTimer) clearInterval(idrTimer)
    const list = [...frames.entries()].sort((a, b) => a[0] - b[0])
    console.log(`videoPkts=${videoPkts} ctrlPkts=${ctrlPkts} otherPkts=${otherPkts} frames=${list.length}`)
    let withParity = 0, mismatch = 0
    for (const [fi, f] of list) {
      if (f.parityShards > 0) withParity++
      if (f.idx.size !== f.dataShards + f.parityShards) mismatch++
    }
    console.log(`frames with parity enabled: ${withParity}/${list.length}`)
    console.log(`frames with shard-count mismatch (loss/dup): ${mismatch}`)
    const big = list.filter(([, f]) => f.dataShards > 10)
    const ratio = (arr) => arr.length
      ? (arr.reduce((s, [, f]) => s + f.parityShards / f.dataShards, 0) / arr.length * 100).toFixed(1) + '%'
      : 'n/a'
    console.log(`avg parity ratio first 30 frames: ${ratio(big.slice(0, 30))}, last 30: ${ratio(big.slice(-30))}`)
    for (const [fi, f] of list.slice(0, 5)) {
      console.log(`  frame ${fi}: recv data=${f.data} parity=${f.parity} (hdr data=${f.dataShards} parity=${f.parityShards})`)
    }
    if (list.length > 5) {
      const [fi, f] = list[list.length - 1]
      console.log(`  frame ${fi}: recv data=${f.data} parity=${f.parity} (hdr data=${f.dataShards} parity=${f.parityShards})`)
    }
    sock.close()
    process.exit(0)
  }, SECONDS * 1000)
})
