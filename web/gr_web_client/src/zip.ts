// store-only ZIP(无压缩)打包:降级模式(无 File System Access API)下
// 文件夹下载无法在浏览器保存时保留目录结构,打包成单个 zip 走 saveBlob。
// 仅支持 <4GB 内容(不写 ZIP64),文件名按 UTF-8 存储(general purpose bit 11)。

const CRC_TABLE = new Uint32Array(256)
for (let n = 0; n < 256; n++) {
  let c = n
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1
  CRC_TABLE[n] = c >>> 0
}

function crc32(data: Uint8Array): number {
  let c = 0xffffffff
  for (let i = 0; i < data.length; i++) {
    c = CRC_TABLE[(c ^ data[i]) & 0xff] ^ (c >>> 8)
  }
  return (c ^ 0xffffffff) >>> 0
}

export interface ZipEntry {
  // zip 内路径,/ 分隔;目录条目以 / 结尾
  name: string
  data: Uint8Array
}

export function buildZip(entries: ZipEntry[]): Uint8Array {
  const encoder = new TextEncoder()
  const chunks: Uint8Array[] = []
  const centrals: Uint8Array[] = []
  let offset = 0
  let total = 0
  for (const e of entries) total += e.data.length
  if (total > 0xffffffff) throw new Error('打包内容超过 4GB,不支持 ZIP64')

  for (const e of entries) {
    const nameBytes = encoder.encode(e.name)
    const crc = crc32(e.data)

    // local file header
    const local = new Uint8Array(30 + nameBytes.length)
    const lv = new DataView(local.buffer)
    lv.setUint32(0, 0x04034b50, true)
    lv.setUint16(4, 20, true) // version needed
    lv.setUint16(6, 0x0800, true) // flags: UTF-8 文件名
    lv.setUint16(8, 0, true) // method: store
    // mod time/date 置 0
    lv.setUint32(14, crc, true)
    lv.setUint32(18, e.data.length, true)
    lv.setUint32(22, e.data.length, true)
    lv.setUint16(26, nameBytes.length, true)
    local.set(nameBytes, 30)
    chunks.push(local, e.data)

    // central directory header
    const central = new Uint8Array(46 + nameBytes.length)
    const cv = new DataView(central.buffer)
    cv.setUint32(0, 0x02014b50, true)
    cv.setUint16(4, 20, true) // version made by
    cv.setUint16(6, 20, true) // version needed
    cv.setUint16(8, 0x0800, true)
    cv.setUint16(10, 0, true)
    cv.setUint32(16, crc, true)
    cv.setUint32(20, e.data.length, true)
    cv.setUint32(24, e.data.length, true)
    cv.setUint16(28, nameBytes.length, true)
    cv.setUint32(42, offset, true) // local header offset
    central.set(nameBytes, 46)
    centrals.push(central)

    offset += local.length + e.data.length
  }

  const cdSize = centrals.reduce((s, c) => s + c.length, 0)
  const end = new Uint8Array(22)
  const ev = new DataView(end.buffer)
  ev.setUint32(0, 0x06054b50, true)
  ev.setUint16(8, entries.length, true)
  ev.setUint16(10, entries.length, true)
  ev.setUint32(12, cdSize, true)
  ev.setUint32(16, offset, true)

  const out = new Uint8Array(offset + cdSize + end.length)
  let pos = 0
  for (const c of chunks) {
    out.set(c, pos)
    pos += c.length
  }
  for (const c of centrals) {
    out.set(c, pos)
    pos += c.length
  }
  out.set(end, pos)
  return out
}
