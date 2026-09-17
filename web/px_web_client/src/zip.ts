// store-only ZIP(无压缩)打包:降级模式(无 File System Access API)下
// 文件夹下载无法在浏览器保存时保留目录结构,打包成单个 zip 走 saveBlob。
// 仅支持 <4GB 内容(不写 ZIP64),文件名按 UTF-8 存储(general purpose bit 11)。

const CRC_TABLE = new Uint32Array(256)
for (let tableIndex = 0; tableIndex < 256; tableIndex++) {
  let checksum = tableIndex
  for (let bitIndex = 0; bitIndex < 8; bitIndex++) {
    checksum = checksum & 1 ? 0xedb88320 ^ (checksum >>> 1) : checksum >>> 1
  }
  CRC_TABLE[tableIndex] = checksum >>> 0
}

function crc32(payloadBytes: Uint8Array): number {
  let checksum = 0xffffffff
  for (let byteIndex = 0; byteIndex < payloadBytes.length; byteIndex++) {
    checksum = CRC_TABLE[(checksum ^ payloadBytes[byteIndex]) & 0xff] ^ (checksum >>> 8)
  }
  return (checksum ^ 0xffffffff) >>> 0
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
  for (const entry of entries) total += entry.data.length
  if (total > 0xffffffff) throw new Error('打包内容超过 4GB,不支持 ZIP64')

  for (const entry of entries) {
    const nameBytes = encoder.encode(entry.name)
    const crc = crc32(entry.data)

    // local file header
    const local = new Uint8Array(30 + nameBytes.length)
    const localView = new DataView(local.buffer)
    localView.setUint32(0, 0x04034b50, true)
    localView.setUint16(4, 20, true) // version needed
    localView.setUint16(6, 0x0800, true) // flags: UTF-8 文件名
    localView.setUint16(8, 0, true) // method: store
    // mod time/date 置 0
    localView.setUint32(14, crc, true)
    localView.setUint32(18, entry.data.length, true)
    localView.setUint32(22, entry.data.length, true)
    localView.setUint16(26, nameBytes.length, true)
    local.set(nameBytes, 30)
    chunks.push(local, entry.data)

    // central directory header
    const central = new Uint8Array(46 + nameBytes.length)
    const centralView = new DataView(central.buffer)
    centralView.setUint32(0, 0x02014b50, true)
    centralView.setUint16(4, 20, true) // version made by
    centralView.setUint16(6, 20, true) // version needed
    centralView.setUint16(8, 0x0800, true)
    centralView.setUint16(10, 0, true)
    centralView.setUint32(16, crc, true)
    centralView.setUint32(20, entry.data.length, true)
    centralView.setUint32(24, entry.data.length, true)
    centralView.setUint16(28, nameBytes.length, true)
    centralView.setUint32(42, offset, true) // local header offset
    central.set(nameBytes, 46)
    centrals.push(central)

    offset += local.length + entry.data.length
  }

  const centralDirectorySize = centrals.reduce(
    (totalSize, centralRecord) => totalSize + centralRecord.length,
    0,
  )
  const endRecord = new Uint8Array(22)
  const endRecordView = new DataView(endRecord.buffer)
  endRecordView.setUint32(0, 0x06054b50, true)
  endRecordView.setUint16(8, entries.length, true)
  endRecordView.setUint16(10, entries.length, true)
  endRecordView.setUint32(12, centralDirectorySize, true)
  endRecordView.setUint32(16, offset, true)

  const archiveBytes = new Uint8Array(offset + centralDirectorySize + endRecord.length)
  let writeOffset = 0
  for (const chunk of chunks) {
    archiveBytes.set(chunk, writeOffset)
    writeOffset += chunk.length
  }
  for (const centralRecord of centrals) {
    archiveBytes.set(centralRecord, writeOffset)
    writeOffset += centralRecord.length
  }
  archiveBytes.set(endRecord, writeOffset)
  return archiveBytes
}
