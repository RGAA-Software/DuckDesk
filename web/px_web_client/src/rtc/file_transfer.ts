// 文件传输客户端(rustdesk 协议语义,阶段 4 Web 主控端全新实现)
// 协议对齐:
//   - web/px_web_client/proto/px_file_transfer.proto(移植自 rustdesk message.proto:355-512)
//   - 对端引擎:src/px_deps/px_ft_engine(对照 rustdesk fs.rs 的 C++ 移植)
// 方向语义(以 rustdesk 源码为准):
//   - FileAction(270):一切"请求"——read_dir/send/receive/create/remove/rename/cancel/send_confirm
//   - FileResponse(280):一切"数据/应答"——dir/block/error/done/digest/empty_dirs
//   - 数据块恒由**读侧**(数据发送方)以 FileResponse.block 发出(fs.rs:1229 new_block):
//     下载时被控发块;上传时主控发块(块走 FileResponse,不是 FileAction!)
//   - 主控上传时,主控读侧逐文件发 FileResponse.digest 报源文件 size/mtime,
//     被控写侧决策后回 FileAction.send_confirm(skip | offset_blk 字节偏移)
// 续传:接收侧凭证在被控引擎(<path>.download/.digest);Web 下载侧只能内存记录已收字节,
//   会话内断线重连可续传(offset_blk),刷新页面整文件重传。
// 通道:ft_data_channel ordered+reliable;每条消息 = NetTlvHeader + px.Message;
//   pkt_index 严格递增(render 按它排序);>128KB 消息 render 侧分片,接收经 TlvReassembler 重组;
//   块载荷 120KB(避免恰在 TLV 分片边界,plan §5.4);发送反压水位 4MB(旧实现实测阈值)。
import { zlibSync, unzlibSync } from 'fflate'
import CryptoJS from 'crypto-js'
import { packTlv, TlvReassembler } from './tlv'
import {
  encodeMessage,
  decodeMessage,
  MSG_TYPE_FILE_ACTION,
  MSG_TYPE_FILE_RESPONSE,
} from './proto'

// px.FileType(px_file_transfer.proto)
export const FT_TYPE_DIR = 0
export const FT_TYPE_DIR_LINK = 2
export const FT_TYPE_DRIVE = 3
export const FT_TYPE_FILE = 4
export const FT_TYPE_FILE_LINK = 5

// 块载荷(px_ft_engine kBlockPayloadSize,fs.rs BUF_SIZE 128KB 的有意缩减)
export const FT_BLOCK_SIZE = 120 * 1024
// datachannel 发送缓冲水位:超过则等 bufferedamountlow 再继续(旧实现实测:4MB 连发会拖垮 SCTP)
const MAX_BUFFERED_BYTES = 4 * 1024 * 1024
const RESP_TIMEOUT_MS = 30000
// 下载侧会话内续传缓存上限(内存兜底,超限丢弃最旧的)
const RESUME_CACHE_MAX_BYTES = 512 * 1024 * 1024

export interface RemoteFileInfo {
  type: number // FT_TYPE_*
  name: string
  path: string // 远端全路径(/ 分隔)
  size: number
  modifiedTime: number // 秒
  isHidden: boolean
}

// 上传条目:name 为相对顶层项的路径('/' 分隔);单文件上传时顶层项 name 为空串,
// 此时 receive.path 已含文件名(rustdesk 约定:join(base, '') = base)
export interface UploadFileItem {
  name: string
  file: File
  size: number
  modifiedTime: number // 秒
}

export type OverwriteDecision = 'skip' | 'overwrite' | 'resume'

export interface OverwriteRequest {
  jobId: number
  fileNum: number
  path: string // 发生冲突的本地(下载)/远端(上传)文件路径
  isUpload: boolean
  isIdentical: boolean // size+mtime 相同(理论上不会进弹框,预留)
  remoteSize: number
  remoteMtime: number
  localSize: number // 本地已存在文件大小(无则 -1)
  resumableBytes: number // 可续传字节数(0 = 不可续传)
}

export interface FtJob {
  id: number
  direction: 'upload' | 'download'
  displayName: string // 顶层文件名/目录名
  remotePath: string // 上传=目标全路径;下载=远端源路径
  state: 'pending' | 'running' | 'done' | 'error' | 'cancelled'
  fileCount: number
  fileNum: number // 当前文件序号(0-based)
  totalSize: number
  finishedSize: number // 已完成字节(含 skip 的文件,对齐 rustdesk finished_size)
  transferred: number // 实际过网字节
  speedBps: number
  skippedCount: number
  error?: string
}

export interface DownloadedFile {
  name: string // 相对名(单文件下载时为顶层文件名)
  data: Uint8Array
  size: number
  modifiedTime: number
}

export interface FileTransferOptions {
  dc: RTCDataChannel
  deviceId: string
  streamId: string
  onLog?: (msg: string) => void
  onJobsChanged?: (jobs: FtJob[]) => void
  // 覆盖冲突决策(下载=本地已有同名不同内容文件;上传=对端报回冲突,当前 render 引擎
  // 对上传冲突直接自动 skip,此回调主要为下载方向与未来引擎升级预留)
  onOverwriteRequest?: (req: OverwriteRequest) => Promise<OverwriteDecision>
  // 下载文件收齐回调(逐文件,写盘/打包由上层决定)
  onFileDownloaded?: (jobId: number, file: DownloadedFile) => Promise<void>
  // 下载决策用:探测本地目标文件(FS Access 模式);返回 null 表示不存在
  localFileProbe?: (jobId: number, name: string) => Promise<{ size: number; mtime: number } | null>
  // 下载会话内续传缓存(断线重连后新 client 复用);由上层持有,刷新页面即丢
  resumeStore?: Map<string, { data: Uint8Array; size: number; mtime: number }>
}

interface PendingReq {
  resolve: (v: never) => void
  reject: (e: Error) => void
  timer: number
}

interface ReadDirPending {
  path: string
  resolve: (r: { path: string; files: RemoteFileInfo[] }) => void
  reject: (e: Error) => void
  timer: number
}

interface UploadJobState {
  items: UploadFileItem[]
  remoteTo: string
  isResume: boolean
  // 当前文件的确认等待(发完 digest 等 send_confirm)
  confirmWait: {
    fileNum: number
    resolve: (r: { skip: boolean; offset: number }) => void
    reject: (e: Error) => void
    timer: number
  } | null
  cancelled: boolean
  activated: boolean
}

interface DownloadJobState {
  remoteFrom: string
  files: Array<{ name: string; size: number; mtime: number }>
  gotDir: boolean
  dirWait: { resolve: () => void; reject: (e: Error) => void; timer: number } | null
  curFileNum: number // 正在接收的文件序号;-1 = 未开始
  curChunks: Uint8Array[]
  curReceived: number // 当前文件已收(解压后)字节
  curSkipped: boolean
  activated: boolean
  cancelled: boolean
}

function toNum(v: unknown): number {
  if (typeof v === 'number') return v
  if (v && typeof (v as { toString(): string }).toString === 'function') {
    return Number((v as { toString(): string }).toString())
  }
  return 0
}

// 已压缩格式后缀跳过压缩(ft_compress.cpp IsCompressedFile,fs.rs:454)
const COMPRESSED_EXTS = new Set(['xz', 'gz', 'zip', '7z', 'rar', 'bz2', 'tgz', 'png', 'jpg'])
function isCompressedName(name: string): boolean {
  const i = name.lastIndexOf('.')
  return i >= 0 && COMPRESSED_EXTS.has(name.slice(i + 1).toLowerCase())
}

// 远端路径拼接(统一 '/' 分隔;处理盘符根 "C:" 与 "/")
export function joinRemote(dir: string, name: string): string {
  if (!dir || dir === '/') return name
  return dir.replace(/[\\/]+$/, '') + '/' + name
}

export function parentRemote(path: string): string {
  const p = path.replace(/[\\/]+$/, '')
  if (!p || p === '/') return '/'
  const idx = p.lastIndexOf('/')
  // "C:/x" 上一级是 "C:/";"C:" 的上一级是盘符列表 "/"
  if (idx <= 0) return '/'
  if (idx === 2 && p[1] === ':') return p.slice(0, 3)
  return p.slice(0, idx)
}

export function sha256HexSoftware(data: Uint8Array): string {
  const words: number[] = []
  for (let index = 0; index < data.length; index += 1) {
    words[index >>> 2] = (words[index >>> 2] || 0)
      | (data[index] << (24 - (index % 4) * 8))
  }
  return CryptoJS.SHA256(CryptoJS.lib.WordArray.create(words, data.length))
    .toString(CryptoJS.enc.Hex)
}

export async function sha256Hex(data: Uint8Array): Promise<string> {
  // Render 的局域网 HTTP origin 不是浏览器安全上下文，可能没有
  // crypto.subtle。完整性校验不能因此退化成空字符串相等。
  if (typeof crypto === 'undefined' || !crypto.subtle) {
    return sha256HexSoftware(data)
  }
  const digest = await crypto.subtle.digest('SHA-256', data.slice().buffer)
  return Array.from(new Uint8Array(digest))
    .map((b) => b.toString(16).padStart(2, '0'))
    .join('')
}

export class FileTransferClient {
  private opts: FileTransferOptions
  private reassembler = new TlvReassembler()
  private pktIndex = 0n
  private idSeq = 0

  private jobs = new Map<number, FtJob>()
  private uploadJobs = new Map<number, UploadJobState>()
  private downloadJobs = new Map<number, DownloadJobState>()
  private activeJobId = 0 // 0 = 无活动作业(is_last_job 挂起语义:单作业推进)
  private jobsDirty = false

  private readDirQueue: ReadDirPending[] = [] // read_dir 无 id,按序配对(通道有序)
  private pendingOps = new Map<number, PendingReq>() // create/remove/rename: done/error 按 id
  private pendingAllFiles = new Map<number, PendingReq>()
  private pendingEmptyDirs = new Map<string, PendingReq>()

  private resumeStore: Map<string, { data: Uint8Array; size: number; mtime: number }>
  private speedTimer = 0
  private dead = false // failAll 后置位:不再激活新作业、不再发送

  constructor(opts: FileTransferOptions) {
    this.opts = opts
    this.resumeStore = opts.resumeStore ?? new Map()
    // 速度:1s 差值法(io_loop.rs:1048 update_jobs_status)
    this.speedTimer = window.setInterval(() => this.updateSpeeds(), 1000)
  }

  private log(msg: string) {
    this.opts.onLog?.(`[ft] ${msg}`)
  }

  private nextId(): number {
    return ++this.idSeq
  }

  // ---------- 收发基础 ----------

  private sendAction(action: Record<string, unknown>) {
    this.sendMessage({ type: MSG_TYPE_FILE_ACTION, fileAction: action })
  }

  private sendResponse(response: Record<string, unknown>) {
    this.sendMessage({ type: MSG_TYPE_FILE_RESPONSE, fileResponse: response })
  }

  private sendMessage(fields: Record<string, unknown>) {
    if (this.dead) throw new Error('文件传输通道已关闭')
    const payload = encodeMessage({
      deviceId: this.opts.deviceId,
      streamId: this.opts.streamId,
      ...fields,
    })
    // ft 通道 pkt_index 严格递增(render 按它排序投递)
    this.opts.dc.send(packTlv(payload, this.pktIndex++))
  }

  // App.vue 把 ft_data_channel 的 onmessage 直接接到这里
  handleChannelMessage(buf: ArrayBuffer) {
    for (const payload of this.reassembler.feed(buf)) {
      let msg: ReturnType<typeof decodeMessage>
      try {
        msg = decodeMessage(payload)
      } catch (err) {
        this.log(`消息解码失败: ${String(err)}`)
        continue
      }
      try {
        this.dispatch(msg)
      } catch (err) {
        this.log(`消息处理失败(type=${msg.type}): ${String(err)}`)
      }
    }
  }

  private dispatch(msg: ReturnType<typeof decodeMessage>) {
    const m = msg as unknown as {
      type: number
      fileAction?: { sendConfirm?: { id: number; fileNum: number; skip?: boolean; offsetBlk?: number } }
      fileResponse?: {
        dir?: { id: number; path: string; entries?: Array<Record<string, unknown>> }
        block?: { id: number; fileNum: number; data?: Uint8Array; compressed?: boolean }
        error?: { id: number; error: string; fileNum: number }
        done?: { id: number; fileNum: number }
        digest?: {
          id: number
          fileNum: number
          lastModified: unknown
          fileSize: unknown
          isUpload?: boolean
          isIdentical?: boolean
          transferredSize: unknown
          isResume?: boolean
        }
        emptyDirs?: { path: string; emptyDirs?: Array<{ path: string }> }
      }
    }
    if (m.type === MSG_TYPE_FILE_ACTION) {
      // 被控写侧对上传 digest 的自动决策回包(IsSame/NoSuchFile/skip)
      if (m.fileAction?.sendConfirm) this.onSendConfirm(m.fileAction.sendConfirm)
      return
    }
    if (m.type !== MSG_TYPE_FILE_RESPONSE || !m.fileResponse) return
    const r = m.fileResponse
    if (r.dir) this.onDir(r.dir)
    else if (r.block) this.onBlock(r.block)
    else if (r.error) this.onError(r.error)
    else if (r.done) this.onDone(r.done)
    else if (r.digest) void this.onDigest(r.digest)
    else if (r.emptyDirs) this.onEmptyDirs(r.emptyDirs)
  }

  // ---------- 作业状态 / 通知 ----------

  private touchJob(job: FtJob) {
    // 保留 speedBps:它由 updateSpeeds 写在 map 内的副本上,避免被作业侧旧对象覆盖;
    // 非 running 终态时以作业侧显式置 0 为准
    const prev = this.jobs.get(job.id)
    const speedBps = job.state === 'running' ? (prev?.speedBps ?? job.speedBps) : job.speedBps
    this.jobs.set(job.id, { ...job, speedBps })
    this.markJobsDirty()
  }

  private markJobsDirty() {
    if (this.jobsDirty) return
    this.jobsDirty = true
    queueMicrotask(() => {
      this.jobsDirty = false
      this.opts.onJobsChanged?.(Array.from(this.jobs.values()))
    })
  }

  private emitJobs() {
    this.opts.onJobsChanged?.(Array.from(this.jobs.values()))
  }

  getJobs(): FtJob[] {
    return Array.from(this.jobs.values())
  }

  private updateSpeeds() {
    const now = Date.now()
    let changed = false
    for (const job of this.jobs.values()) {
      if (job.state !== 'running') {
        if (job.speedBps !== 0) {
          job.speedBps = 0
          changed = true
        }
        continue
      }
      const prev = this.speedSamples.get(job.id)
      if (prev && now > prev.time) {
        job.speedBps = Math.max(0, Math.round(((job.finishedSize - prev.bytes) * 1000) / (now - prev.time)))
        changed = true
      }
      this.speedSamples.set(job.id, { time: now, bytes: job.finishedSize })
    }
    if (changed) this.emitJobs()
  }

  private speedSamples = new Map<number, { time: number; bytes: number }>()

  clearFinishedJobs() {
    for (const [id, j] of this.jobs) {
      if (j.state !== 'running' && j.state !== 'pending') {
        this.jobs.delete(id)
        this.speedSamples.delete(id)
      }
    }
    this.emitJobs()
  }

  // ---------- 目录操作 ----------

  // 列目录:path='/' 在 Windows 下列盘符(fs.rs:35)
  listDir(path: string, includeHidden = false): Promise<{ path: string; files: RemoteFileInfo[] }> {
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.readDirQueue = this.readDirQueue.filter((p) => p.path !== path || p.timer !== timer)
        reject(new Error(`列目录超时: ${path}`))
      }, RESP_TIMEOUT_MS)
      this.readDirQueue.push({ path, resolve, reject, timer })
      this.sendAction({ readDir: { path, includeHidden } })
    })
  }

  // 递归列出远端目录下全部文件(ReadAllFiles,响应 dir.id 配对)
  readAllFiles(path: string): Promise<RemoteFileInfo[]> {
    const id = this.nextId()
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.pendingAllFiles.delete(id)
        reject(new Error(`递归列目录超时: ${path}`))
      }, RESP_TIMEOUT_MS * 4)
      this.pendingAllFiles.set(id, {
        resolve: resolve as PendingReq['resolve'],
        reject,
        timer,
      })
      this.sendAction({ allFiles: { id, path, includeHidden: false } })
    })
  }

  // 远端目录下的空目录列表(下载文件夹时还原空目录用)
  readEmptyDirs(path: string): Promise<string[]> {
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.pendingEmptyDirs.delete(path)
        reject(new Error(`读取空目录超时: ${path}`))
      }, RESP_TIMEOUT_MS)
      this.pendingEmptyDirs.set(path, {
        resolve: resolve as PendingReq['resolve'],
        reject,
        timer,
      })
      this.sendAction({ readEmptyDirs: { path, includeHidden: false } })
    })
  }

  private waitOp(id: number, timeoutError: string): Promise<void> {
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.pendingOps.delete(id)
        reject(new Error(timeoutError))
      }, RESP_TIMEOUT_MS)
      this.pendingOps.set(id, {
        resolve: resolve as PendingReq['resolve'],
        reject,
        timer,
      })
    })
  }

  createDir(path: string): Promise<void> {
    const id = this.nextId()
    const p = this.waitOp(id, `新建文件夹超时: ${path}`)
    this.sendAction({ create: { id, path } })
    return p
  }

  removeDir(path: string, recursive: boolean): Promise<void> {
    const id = this.nextId()
    const p = this.waitOp(id, `删除目录超时: ${path}`)
    this.sendAction({ removeDir: { id, path, recursive } })
    return p
  }

  removeFile(path: string): Promise<void> {
    const id = this.nextId()
    const p = this.waitOp(id, `删除文件超时: ${path}`)
    this.sendAction({ removeFile: { id, path, fileNum: 0 } })
    return p
  }

  rename(path: string, newName: string): Promise<void> {
    const id = this.nextId()
    const p = this.waitOp(id, `重命名超时: ${path}`)
    this.sendAction({ rename: { id, path, newName } })
    return p
  }

  // ---------- 上传(主控读侧) ----------

  // items 已由上层递归展开;remoteTo = 远端目标全路径(含顶层名)
  upload(items: UploadFileItem[], remoteTo: string, displayName: string, isResume = false): FtJob {
    const id = this.nextId()
    const job: FtJob = {
      id,
      direction: 'upload',
      displayName,
      remotePath: remoteTo,
      state: 'pending',
      fileCount: items.length,
      fileNum: 0,
      totalSize: items.reduce((s, it) => s + it.size, 0),
      finishedSize: 0,
      transferred: 0,
      speedBps: 0,
      skippedCount: 0,
    }
    this.jobs.set(id, job)
    this.uploadJobs.set(id, {
      items,
      remoteTo,
      isResume,
      confirmWait: null,
      cancelled: false,
      activated: false,
    })
    this.emitJobs()
    this.maybeActivateNext()
    return job
  }

  private activateUpload(id: number) {
    const st = this.uploadJobs.get(id)
    const job = this.jobs.get(id)
    if (!st || !job) return
    st.activated = true
    job.state = 'running'
    this.touchJob(job)
    // FileAction.receive:对端建写作业(fs.rs new_write)
    this.sendAction({
      receive: {
        id,
        path: st.remoteTo,
        files: st.items.map((it) => ({
          entryType: FT_TYPE_FILE,
          name: it.name,
          size: it.size,
          modifiedTime: it.modifiedTime,
        })),
        fileNum: 0,
        totalSize: job.totalSize,
      },
    })
    void this.runUpload(id).catch((err) => {
      this.failJob(id, err instanceof Error ? err.message : String(err))
    })
  }

  private async runUpload(id: number) {
    const st = this.uploadJobs.get(id)
    const job = this.jobs.get(id)
    if (!st || !job) return

    for (let i = 0; i < st.items.length; i++) {
      if (st.cancelled) return
      const item = st.items[i]
      job.fileNum = i
      this.touchJob(job)

      // 逐文件 digest 握手(覆盖检测):必须先挂起确认等待再发送。
      // RTC 本机/局域网回包可以在 send() 返回前同步触发 onmessage；
      // 如果先发后挂 await，send_confirm 会成为丢失唤醒，作业永久停在 0 字节。
      const confirm = await new Promise<{ skip: boolean; offset: number }>((resolve, reject) => {
        const timer = window.setTimeout(() => {
          if (st.confirmWait?.fileNum === i) {
            st.confirmWait = null
          }
          reject(new Error(`等待远端文件确认超时: ${item.name || st.remoteTo}`))
        }, RESP_TIMEOUT_MS)
        const waiter = { fileNum: i, resolve, reject, timer }
        st.confirmWait = waiter
        try {
          this.sendResponse({
            digest: {
              id,
              fileNum: i,
              lastModified: item.modifiedTime,
              fileSize: item.size,
              isResume: st.isResume,
            },
          })
        } catch (err) {
          window.clearTimeout(timer)
          if (st.confirmWait === waiter) st.confirmWait = null
          reject(err instanceof Error ? err : new Error(String(err)))
        }
      })
      st.confirmWait = null
      if (st.cancelled) return
      if (confirm.skip) {
        job.skippedCount++
        job.finishedSize += item.size
        this.touchJob(job)
        continue
      }

      // 读侧按字节偏移定位(续传;offset_blk 名为块号实为字节偏移,plan §5.1)
      let pos = confirm.offset
      if (pos > 0) {
        job.finishedSize += pos
        job.transferred += pos
      }
      while (pos < item.size) {
        if (st.cancelled) return
        await this.waitSendBuffer()
        if (st.cancelled) return
        const end = Math.min(pos + FT_BLOCK_SIZE, item.size)
        const raw = new Uint8Array(await item.file.slice(pos, end).arrayBuffer())
        // 发送侧压缩:zlib deflate(与对端 miniz mz_compress2 格式一致);
        // 已压缩后缀或不划算时发原始块
        let data = raw
        let compressed = false
        if (!isCompressedName(item.name)) {
          const c = zlibSync(raw, { level: 6 })
          if (c.length > 0 && c.length < raw.length) {
            data = c
            compressed = true
          }
        }
        this.sendResponse({ block: { id, fileNum: i, data, compressed } })
        job.finishedSize += raw.length
        job.transferred += data.length
        this.markJobsDirty()
        pos = end
      }
      // EOF:发空数据块(旧 file_num),写侧靠后续块的新 file_num 推进(fs.rs:1001)
      this.sendResponse({ block: { id, fileNum: i, data: new Uint8Array(0), compressed: false } })
    }
    // 全部文件读完:作业完成(fs.rs handle_read_jobs -> new_done)
    this.sendResponse({ done: { id, fileNum: st.items.length } })
    job.state = 'done'
    job.fileNum = st.items.length
    job.speedBps = 0
    this.touchJob(job)
    this.log(`上传完成: ${job.displayName} (${job.finishedSize} bytes, 跳过 ${job.skippedCount})`)
    this.finishJob(id)
  }

  // ---------- 下载(主控写侧) ----------

  // remoteFrom = 远端源路径(文件或目录);displayName = 顶层名
  download(remoteFrom: string, displayName: string): FtJob {
    const id = this.nextId()
    const job: FtJob = {
      id,
      direction: 'download',
      displayName,
      remotePath: remoteFrom,
      state: 'pending',
      fileCount: 0,
      fileNum: 0,
      totalSize: 0,
      finishedSize: 0,
      transferred: 0,
      speedBps: 0,
      skippedCount: 0,
    }
    this.jobs.set(id, job)
    this.downloadJobs.set(id, {
      remoteFrom,
      files: [],
      gotDir: false,
      dirWait: null,
      curFileNum: -1,
      curChunks: [],
      curReceived: 0,
      curSkipped: false,
      activated: false,
      cancelled: false,
    })
    this.emitJobs()
    this.maybeActivateNext()
    return job
  }

  private activateDownload(id: number) {
    const st = this.downloadJobs.get(id)
    const job = this.jobs.get(id)
    if (!st || !job) return
    st.activated = true
    job.state = 'running'
    this.touchJob(job)
    // FileAction.send:请对端发送文件(对端建读作业,先回 dir 文件清单)
    this.sendAction({
      send: { id, path: st.remoteFrom, includeHidden: false, fileNum: 0, fileType: 0 },
    })
  }

  // 下载文件清单到达(FileResponse.dir,connection.rs:5295 语义)
  private onDir(fd: { id: number; path: string; entries?: Array<Record<string, unknown>> }) {
    // 优先配对下载作业(对端回的作业文件列表)
    const st = this.downloadJobs.get(fd.id)
    if (st && !st.gotDir) {
      st.gotDir = true
      st.files = (fd.entries ?? []).map((e) => ({
        name: String(e.name ?? ''),
        size: toNum(e.size),
        mtime: toNum(e.modifiedTime),
      }))
      const job = this.jobs.get(fd.id)
      if (job) {
        job.fileCount = st.files.length
        job.totalSize = st.files.reduce((s, f) => s + f.size, 0)
        this.touchJob(job)
      }
      if (st.dirWait) {
        window.clearTimeout(st.dirWait.timer)
        st.dirWait.resolve()
        st.dirWait = null
      }
      return
    }
    // ReadAllFiles 配对
    const pendingAll = this.pendingAllFiles.get(fd.id)
    if (pendingAll) {
      this.pendingAllFiles.delete(fd.id)
      window.clearTimeout(pendingAll.timer)
      const files = (fd.entries ?? []).map((e) => ({
        type: toNum(e.entryType),
        name: String(e.name ?? ''),
        path: joinRemote(fd.path, String(e.name ?? '')),
        size: toNum(e.size),
        modifiedTime: toNum(e.modifiedTime),
        isHidden: !!e.isHidden,
      }))
      pendingAll.resolve(files as never)
      return
    }
    // read_dir FIFO(read_dir 无 id,回包 id=0;通道有序 + 对端单 worker 串行处理)
    const pending = this.readDirQueue.shift()
    if (pending) {
      window.clearTimeout(pending.timer)
      pending.resolve({
        path: fd.path || pending.path,
        files: (fd.entries ?? []).map((e) => ({
          type: toNum(e.entryType),
          name: String(e.name ?? ''),
          path: joinRemote(fd.path, String(e.name ?? '')),
          size: toNum(e.size),
          modifiedTime: toNum(e.modifiedTime),
          isHidden: !!e.isHidden,
        })),
      })
      return
    }
    this.log(`收到无对应请求的 dir 响应 id=${fd.id} path=${fd.path}`)
  }

  // digest 到达:下载方向(写侧决策)与上传方向(读侧被报回冲突)两种
  private async onDigest(d: {
    id: number
    fileNum: number
    lastModified: unknown
    fileSize: unknown
    isUpload?: boolean
    isIdentical?: boolean
    transferredSize: unknown
    isResume?: boolean
  }) {
    if (d.isUpload) {
      // 上传方向:对端(写侧)报回它本地的同名文件情况(ui_cm_interface.rs:1116 语义;
      // 当前 render 引擎对冲突自动 skip 不走此分支,此处为完整性与未来升级实现)
      const st = this.uploadJobs.get(d.id)
      const job = this.jobs.get(d.id)
      if (!st || !job || !st.confirmWait || st.confirmWait.fileNum !== d.fileNum) return
      const item = st.items[d.fileNum]
      const resumeBytes = d.isIdentical && d.isResume ? toNum(d.transferredSize) : 0
      let decision: OverwriteDecision
      const strategy = this.uploadStrategy
      if (resumeBytes > 0 && strategy !== 'skip') {
        decision = 'resume'
      } else if (strategy) {
        decision = strategy
      } else if (this.opts.onOverwriteRequest) {
        decision = await this.opts.onOverwriteRequest({
          jobId: d.id,
          fileNum: d.fileNum,
          path: joinRemote(st.remoteTo, item?.name ?? ''),
          isUpload: true,
          isIdentical: !!d.isIdentical,
          remoteSize: toNum(d.fileSize),
          remoteMtime: toNum(d.lastModified),
          localSize: item?.size ?? -1,
          resumableBytes: resumeBytes,
        })
      } else {
        decision = 'skip'
      }
      const offset = decision === 'resume' ? resumeBytes : 0
      const skip = decision === 'skip'
      this.sendAction({ sendConfirm: { id: d.id, fileNum: d.fileNum, ...(skip ? { skip: true } : { offsetBlk: offset }) } })
      window.clearTimeout(st.confirmWait.timer)
      st.confirmWait.resolve({ skip, offset })
      return
    }

    // 下载方向:对端(读侧)报源文件 digest,本地做覆盖/续传决策(ft_engine.cpp:418)
    const st = this.downloadJobs.get(d.id)
    const job = this.jobs.get(d.id)
    if (!st || !job) return
    const entry = st.files[d.fileNum]
    if (!entry) return
    const fileSize = toNum(d.fileSize)
    const lastModified = toNum(d.lastModified)
    const displayName = entry.name || job.displayName

    // 新文件的 digest 到达意味着上一文件已全部收完(通道有序),先收尾
    if (st.curFileNum >= 0 && st.curFileNum !== d.fileNum) {
      await this.finalizeCurrentFile(d.id)
      if (st.cancelled || !this.downloadJobs.has(d.id)) return
    }

    // 会话内续传:内存里有同名同 size/mtime 的部分数据 -> 直接 offset 续传
    const resumeKey = `${st.remoteFrom}\n${entry.name}`
    const cached = this.resumeStore.get(resumeKey)
    let offset = 0
    let skip = false
    if (cached && cached.size === fileSize && cached.mtime === lastModified && cached.data.length > 0 && cached.data.length < fileSize) {
      offset = cached.data.length
      st.curChunks = [cached.data]
      st.curReceived = cached.data.length
      this.resumeStore.delete(resumeKey)
      this.log(`续传: ${displayName} 从 ${offset} 字节继续`)
    } else {
      if (cached) this.resumeStore.delete(resumeKey) // 内容已变,丢弃旧缓存
      st.curChunks = []
      st.curReceived = 0
      // 本地已有文件探测(FS Access 模式):identical -> skip;不同 -> 弹框
      const probe = this.opts.localFileProbe
        ? await this.opts.localFileProbe(d.id, displayName)
        : null
      if (probe && probe.size === fileSize && probe.mtime === lastModified) {
        skip = true
      } else if (probe) {
        let decision: OverwriteDecision
        if (this.downloadStrategy) {
          decision = this.downloadStrategy
        } else if (this.opts.onOverwriteRequest) {
          decision = await this.opts.onOverwriteRequest({
            jobId: d.id,
            fileNum: d.fileNum,
            path: displayName,
            isUpload: false,
            isIdentical: false,
            remoteSize: fileSize,
            remoteMtime: lastModified,
            localSize: probe.size,
            resumableBytes: 0,
          })
        } else {
          decision = 'overwrite'
        }
        skip = decision === 'skip'
      }
    }
    if (st.cancelled) return
    st.curFileNum = d.fileNum
    st.curSkipped = skip
    job.fileNum = d.fileNum
    if (skip) {
      job.skippedCount++
      job.finishedSize += fileSize
    } else if (offset > 0) {
      job.finishedSize += offset
      job.transferred += offset
    }
    this.touchJob(job)
    // FileAction.send_confirm:回给对端读作业(kSendConfirm -> read_jobs Confirm)
    this.sendAction({
      sendConfirm: { id: d.id, fileNum: d.fileNum, ...(skip ? { skip: true } : { offsetBlk: offset }) },
    })
  }

  // 上传读侧收到对端写侧的确认(FileAction.send_confirm;render 自动决策或主控 UI 决策的回包)
  private onSendConfirm(c: { id: number; fileNum: number; skip?: boolean; offsetBlk?: number }) {
    const st = this.uploadJobs.get(c.id)
    if (!st || !st.confirmWait) return
    if (st.confirmWait.fileNum !== c.fileNum) return // 非当前文件的 confirm 忽略(fs.rs:1157)
    window.clearTimeout(st.confirmWait.timer)
    const skip = c.skip === true
    const offset = skip ? 0 : toNum(c.offsetBlk)
    st.confirmWait.resolve({ skip, offset })
  }

  private onBlock(b: { id: number; fileNum: number; data?: Uint8Array; compressed?: boolean }) {
    const st = this.downloadJobs.get(b.id)
    const job = this.jobs.get(b.id)
    if (!st || !job || st.cancelled) return
    // 块切到新文件:收尾上一文件(fs.rs:760 write 内 modify_time 语义)。
    // 正常流程在 onDigest 里已收尾,这里是防御性兜底(如对端跳过 digest 直发块)
    if (st.curFileNum >= 0 && b.fileNum !== st.curFileNum) {
      void this.finalizeCurrentFile(b.id)
      st.curFileNum = b.fileNum
      job.fileNum = b.fileNum
    }
    const data = b.data
    if (!data || data.length === 0) return // EOF 空块
    let chunk: Uint8Array
    if (b.compressed) {
      try {
        chunk = unzlibSync(data)
      } catch (err) {
        this.failJob(b.id, `解压失败: ${err instanceof Error ? err.message : String(err)}`)
        return
      }
    } else {
      chunk = data
    }
    if (st.curSkipped) return // 防御:skip 的文件不应有块
    st.curChunks.push(chunk)
    st.curReceived += chunk.length
    job.finishedSize += chunk.length
    job.transferred += data.length
    this.markJobsDirty()
  }

  // 当前文件收齐:快照缓冲区后交付上层落盘;失败中断作业
  private async finalizeCurrentFile(id: number) {
    const st = this.downloadJobs.get(id)
    const job = this.jobs.get(id)
    if (!st || !job || st.curFileNum < 0) return
    const fileNum = st.curFileNum
    const skipped = st.curSkipped
    const chunks = st.curChunks
    const received = st.curReceived
    st.curFileNum = -1
    st.curChunks = []
    st.curReceived = 0
    st.curSkipped = false
    if (skipped) return
    const entry = st.files[fileNum]
    const name = entry?.name ?? ''
    const data = concatChunks(chunks, received)
    if (entry && entry.size > 0 && data.length !== entry.size) {
      this.failJob(id, `大小校验失败: ${name} ${data.length} != ${entry.size}`)
      return
    }
    try {
      await this.opts.onFileDownloaded?.(id, {
        name: name || job.displayName,
        data,
        size: data.length,
        modifiedTime: entry?.mtime ?? 0,
      })
    } catch (err) {
      this.failJob(id, `写入本地失败: ${err instanceof Error ? err.message : String(err)}`)
    }
  }

  private onDone(d: { id: number; fileNum: number }) {
    // 目录操作(create/remove/rename)的完成回包
    const op = this.pendingOps.get(d.id)
    if (op) {
      this.pendingOps.delete(d.id)
      window.clearTimeout(op.timer)
      op.resolve(undefined as never)
      return
    }
    // 下载作业完成
    const st = this.downloadJobs.get(d.id)
    const job = this.jobs.get(d.id)
    if (!st || !job) return
    void (async () => {
      if (st.curFileNum >= 0 && !st.cancelled) {
        await this.finalizeCurrentFile(d.id)
      }
      // finalize 可能已把作业置为 error(落盘失败)
      const cur = this.jobs.get(d.id)
      if (!cur || cur.state !== 'running') return
      cur.state = 'done'
      cur.speedBps = 0
      this.touchJob(cur)
      this.log(`下载完成: ${cur.displayName} (${cur.finishedSize} bytes, 跳过 ${cur.skippedCount})`)
      this.finishJob(d.id)
    })()
  }

  private onError(e: { id: number; error: string; fileNum: number }) {
    const op = this.pendingOps.get(e.id)
    if (op) {
      this.pendingOps.delete(e.id)
      window.clearTimeout(op.timer)
      op.reject(new Error(e.error))
      return
    }
    const pendingAll = this.pendingAllFiles.get(e.id)
    if (pendingAll) {
      this.pendingAllFiles.delete(e.id)
      window.clearTimeout(pendingAll.timer)
      pendingAll.reject(new Error(e.error))
      return
    }
    if (this.jobs.has(e.id)) {
      this.failJob(e.id, e.error)
      return
    }
    this.log(`对端错误: id=${e.id} ${e.error}`)
  }

  private onEmptyDirs(r: { path: string; emptyDirs?: Array<{ path: string }> }) {
    const pending = this.pendingEmptyDirs.get(r.path)
    if (!pending) return
    this.pendingEmptyDirs.delete(r.path)
    window.clearTimeout(pending.timer)
    pending.resolve((r.emptyDirs ?? []).map((d) => d.path) as never)
  }

  // ---------- 作业调度(is_last_job 语义:单活动作业,其余排队) ----------

  private maybeActivateNext() {
    if (this.dead || this.activeJobId !== 0) return
    for (const job of this.jobs.values()) {
      if (job.state !== 'pending') continue
      this.activeJobId = job.id
      try {
        if (job.direction === 'upload') this.activateUpload(job.id)
        else this.activateDownload(job.id)
      } catch (err) {
        this.activeJobId = 0
        this.failJob(job.id, err instanceof Error ? err.message : String(err))
      }
      return
    }
  }

  private finishJob(id: number) {
    this.speedSamples.delete(id)
    if (this.activeJobId === id) {
      this.activeJobId = 0
      if (!this.dead) this.maybeActivateNext()
    }
  }

  private failJob(id: number, error: string) {
    const job = this.jobs.get(id)
    if (!job || job.state === 'error' || job.state === 'cancelled') return
    job.state = 'error'
    job.error = error
    job.speedBps = 0
    // 解开所有等待,让异步循环自行退出
    const up = this.uploadJobs.get(id)
    if (up) {
      up.cancelled = true
      if (up.confirmWait) window.clearTimeout(up.confirmWait.timer)
      up.confirmWait?.resolve({ skip: true, offset: 0 })
      up.confirmWait = null
    }
    this.touchJob(job)
    this.log(`作业失败: ${job.displayName}: ${error}`)
    // 下载中断:保留已收部分进续传缓存(显式取消不清——那是 cancel 的路径)
    const st = this.downloadJobs.get(id)
    if (st) {
      st.dirWait?.reject(new Error(error))
      st.dirWait = null
      if (st.curFileNum >= 0 && !st.curSkipped && st.curReceived > 0) {
        const entry = st.files[st.curFileNum]
        if (entry) {
          this.putResumeCache(`${st.remoteFrom}\n${entry.name}`, {
            data: concatChunks(st.curChunks, st.curReceived),
            size: entry.size,
            mtime: entry.mtime,
          })
        }
      }
    }
    this.finishJob(id)
  }

  // 取消作业:发 FileAction.cancel(对端写作业清 .download/.digest;读作业直接移除)
  cancel(id: number) {
    const job = this.jobs.get(id)
    if (!job || (job.state !== 'running' && job.state !== 'pending')) return
    const up = this.uploadJobs.get(id)
    const down = this.downloadJobs.get(id)
    const activated = up?.activated || down?.activated
    if (up) {
      up.cancelled = true
      if (up.confirmWait) window.clearTimeout(up.confirmWait.timer)
      up.confirmWait?.resolve({ skip: true, offset: 0 }) // 解开等待,runUpload 自行退出
      up.confirmWait = null
    }
    if (down) {
      down.cancelled = true
      down.dirWait?.reject(new Error('已取消'))
    }
    job.state = 'cancelled'
    job.speedBps = 0
    this.touchJob(job)
    if (activated) {
      this.sendAction({ cancel: { id } })
    }
    // 显式取消:清除该作业的续传缓存(对齐写侧 RemoveDownloadFile 语义)
    if (down) {
      for (const f of down.files) {
        this.resumeStore.delete(`${down.remoteFrom}\n${f.name}`)
      }
    }
    this.log(`已取消: ${job.displayName}`)
    this.finishJob(id)
  }

  // "应用到全部"覆盖策略(UI 勾选后设置,后续冲突不再弹框)
  uploadStrategy: OverwriteDecision | null = null
  downloadStrategy: OverwriteDecision | null = null
  setOverwriteStrategy(direction: 'upload' | 'download', s: OverwriteDecision | null) {
    if (direction === 'upload') this.uploadStrategy = s
    else this.downloadStrategy = s
  }

  // ---------- 反压 ----------

  // datachannel 发送缓冲高水位等待(块级无 ack,这是唯一的流控)
  private waitSendBuffer(): Promise<void> {
    const dc = this.opts.dc
    if (dc.readyState !== 'open') return Promise.reject(new Error('通道已断开'))
    if (dc.bufferedAmount <= MAX_BUFFERED_BYTES) return Promise.resolve()
    return new Promise((resolve, reject) => {
      dc.bufferedAmountLowThreshold = MAX_BUFFERED_BYTES / 2
      const onLow = () => {
        dc.removeEventListener('bufferedamountlow', onLow)
        dc.removeEventListener('close', onClose)
        resolve()
      }
      const onClose = () => {
        dc.removeEventListener('bufferedamountlow', onLow)
        reject(new Error('通道已断开'))
      }
      dc.addEventListener('bufferedamountlow', onLow)
      dc.addEventListener('close', onClose, { once: true })
    })
  }

  // ---------- 续传缓存 ----------

  private putResumeCache(key: string, v: { data: Uint8Array; size: number; mtime: number }) {
    let total = 0
    for (const it of this.resumeStore.values()) total += it.data.length
    // 超限:清最旧的(Map 迭代序 = 插入序)
    while (total + v.data.length > RESUME_CACHE_MAX_BYTES && this.resumeStore.size > 0) {
      const oldest = this.resumeStore.keys().next().value
      if (oldest === undefined) break
      total -= this.resumeStore.get(oldest)?.data.length ?? 0
      this.resumeStore.delete(oldest)
    }
    if (v.data.length <= RESUME_CACHE_MAX_BYTES) this.resumeStore.set(key, v)
  }

  // ---------- 清理 ----------

  // 通道断开/页面清理:失败所有进行中作业与请求;下载已收部分保留续传缓存(断线语义)
  failAll(reason: string) {
    this.dead = true
    this.activeJobId = 0 // 先阻止 failJob -> finishJob 激活排队作业
    for (const p of this.readDirQueue) {
      window.clearTimeout(p.timer)
      p.reject(new Error(reason))
    }
    this.readDirQueue = []
    for (const [id, p] of this.pendingOps) {
      window.clearTimeout(p.timer)
      p.reject(new Error(reason))
      this.pendingOps.delete(id)
    }
    for (const [id, p] of this.pendingAllFiles) {
      window.clearTimeout(p.timer)
      p.reject(new Error(reason))
      this.pendingAllFiles.delete(id)
    }
    for (const [k, p] of this.pendingEmptyDirs) {
      window.clearTimeout(p.timer)
      p.reject(new Error(reason))
      this.pendingEmptyDirs.delete(k)
    }
    for (const id of Array.from(this.jobs.keys())) {
      const job = this.jobs.get(id)
      if (job && (job.state === 'running' || job.state === 'pending')) {
        const up = this.uploadJobs.get(id)
        if (up) {
          up.cancelled = true
          if (up.confirmWait) window.clearTimeout(up.confirmWait.timer)
          up.confirmWait?.resolve({ skip: true, offset: 0 })
          up.confirmWait = null
        }
        const down = this.downloadJobs.get(id)
        if (down) {
          down.cancelled = true
          down.dirWait?.reject(new Error(reason))
        }
        this.failJob(id, reason)
      }
    }
    this.uploadJobs.clear()
    this.downloadJobs.clear()
    this.speedSamples.clear()
    window.clearInterval(this.speedTimer)
  }
}

function concatChunks(chunks: Uint8Array[], total: number): Uint8Array {
  if (chunks.length === 1) return chunks[0]
  const out = new Uint8Array(total)
  let off = 0
  for (const c of chunks) {
    out.set(c, off)
    off += c.length
  }
  return out
}
