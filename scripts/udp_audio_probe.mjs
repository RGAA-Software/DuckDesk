// UDP audio probe: bind to render's udp media channel, dump audio packet seqs.
// Usage: node scripts/udp_audio_probe.mjs [host] [port] [seconds]
import dgram from 'node:dgram'

const HOST = process.argv[2] || '10.0.0.70'
const PORT = Number(process.argv[3] || 20381)
const SECONDS = Number(process.argv[4] || 10)

const MAGIC = 0x4755 // 'GU'
const PKT_VIDEO = 1, PKT_AUDIO = 2, PKT_CTRL = 3
const CTRL_HELLO = 1, CTRL_HEARTBEAT = 2

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

const sock = dgram.createSocket('udp4')
let audioPkts = 0, videoPkts = 0, otherPkts = 0
const seqs = []
let badLen = 0

sock.on('message', (msg) => {
  if (msg.length < 4 || msg.readUInt16LE(0) !== MAGIC) { otherPkts++; return }
  const type = msg[3]
  if (type === PKT_VIDEO) { videoPkts++; return }
  if (type !== PKT_AUDIO) { otherPkts++; return }
  audioPkts++
  if (msg.length < 14) { badLen++; return }
  const seq = msg.readUInt32LE(4)
  const ts = msg.readUInt32LE(8)
  const plen = msg.readUInt16LE(12)
  if (msg.length !== 14 + plen) badLen++
  if (seqs.length < 400) seqs.push([seq, ts, plen, msg.length])
})

sock.bind(0, () => {
  sock.send(buildCtrl(CTRL_HELLO, 'audio-probe-dev', 'audio-probe-stream-1'), PORT, HOST)
  console.log(`hello sent to ${HOST}:${PORT}, listening ${SECONDS}s ...`)
  const hb = setInterval(() => sock.send(buildCtrl(CTRL_HEARTBEAT, 'audio-probe-stream-1', null), PORT, HOST), 1000)
  setTimeout(() => {
    clearInterval(hb)
    console.log(`audioPkts=${audioPkts} videoPkts=${videoPkts} otherPkts=${otherPkts} badLen=${badLen}`)
    if (seqs.length > 0) {
      console.log('first seqs (seq, ts, plen, total):')
      for (const s of seqs.slice(0, 30)) console.log(' ', s.join(', '))
      const deltas = []
      for (let i = 1; i < seqs.length; i++) deltas.push(seqs[i][0] - seqs[i - 1][0])
      const hist = {}
      for (const d of deltas) hist[d] = (hist[d] || 0) + 1
      console.log('seq delta histogram:', JSON.stringify(hist))
      console.log(`seq range: ${seqs[0][0]} .. ${seqs[seqs.length - 1][0]} over ${seqs.length} sampled pkts`)
    }
    sock.close()
  }, SECONDS * 1000)
})
