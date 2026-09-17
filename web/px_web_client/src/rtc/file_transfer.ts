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
  onLog?: (message: string) => void
  onJobsChanged?: (jobs: FtJob[]) => void
  // 覆盖冲突决策(下载=本地已有同名不同内容文件;上传=对端报回冲突,当前 render 引擎
  // 对上传冲突直接自动 skip,此回调主要为下载方向与未来引擎升级预留)
  onOverwriteRequest?: (request: OverwriteRequest) => Promise<OverwriteDecision>
  // 下载文件收齐回调(逐文件,写盘/打包由上层决定)
  onFileDownloaded?: (jobId: number, file: DownloadedFile) => Promise<void>
  // 下载决策用:探测本地目标文件(FS Access 模式);返回 null 表示不存在
  localFileProbe?: (jobId: number, name: string) => Promise<{ size: number; mtime: number } | null>
  // 下载会话内续传缓存(断线重连后新 client 复用);由上层持有,刷新页面即丢
  resumeStore?: Map<string, { data: Uint8Array; size: number; mtime: number }>
}

interface PendingReq {
  resolve: (value: never) => void
  reject: (error: Error) => void
  timer: number
}

interface ReadDirPending {
  path: string
  resolve: (result: { path: string; files: RemoteFileInfo[] }) => void
  reject: (error: Error) => void
  timer: number
}

interface UploadJobState {
  items: UploadFileItem[]
  remoteTo: string
  isResume: boolean
  // 当前文件的确认等待(发完 digest 等 send_confirm)
  confirmWait: {
    fileNum: number
    resolve: (result: { skip: boolean; offset: number }) => void
    reject: (error: Error) => void
    timer: number
  } | null
  cancelled: boolean
  activated: boolean
}

interface DownloadJobState {
  remoteFrom: string
  files: Array<{ name: string; size: number; mtime: number }>
  gotDir: boolean
  dirWait: { resolve: () => void; reject: (error: Error) => void; timer: number } | null
  currentFileNumber: number // 正在接收的文件序号;-1 = 未开始
  currentChunks: Uint8Array[]
  currentReceivedBytes: number // 当前文件已收(解压后)字节
  currentFileSkipped: boolean
  activated: boolean
  cancelled: boolean
}

function toNum(value: unknown): number {
  if (typeof value === 'number') return value
  if (value && typeof (value as { toString(): string }).toString === 'function') {
    return Number((value as { toString(): string }).toString())
  }
  return 0
}

// 已压缩格式后缀跳过压缩(ft_compress.cpp IsCompressedFile,fs.rs:454)
const COMPRESSED_EXTS = new Set(['xz', 'gz', 'zip', '7z', 'rar', 'bz2', 'tgz', 'png', 'jpg'])
function isCompressedName(name: string): boolean {
  const extensionSeparatorIndex = name.lastIndexOf('.')
  return extensionSeparatorIndex >= 0 && COMPRESSED_EXTS.has(name.slice(extensionSeparatorIndex + 1).toLowerCase())
}

// 远端路径拼接(统一 '/' 分隔;处理盘符根 "C:" 与 "/")
export function joinRemote(dir: string, name: string): string {
  if (!dir || dir === '/') return name
  return dir.replace(/[\\/]+$/, '') + '/' + name
}

export function parentRemote(path: string): string {
  const normalizedPath = path.replace(/[\\/]+$/, '')
  if (!normalizedPath || normalizedPath === '/') return '/'
  const separatorIndex = normalizedPath.lastIndexOf('/')
  // "C:/x" 上一级是 "C:/";"C:" 的上一级是盘符列表 "/"
  if (separatorIndex <= 0) return '/'
  if (separatorIndex === 2 && normalizedPath[1] === ':') return normalizedPath.slice(0, 3)
  return normalizedPath.slice(0, separatorIndex)
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
    .map((byteValue) => byteValue.toString(16).padStart(2, '0'))
    .join('')
}

export class FileTransferClient {
  private options: FileTransferOptions
  private reassembler = new TlvReassembler()
  private packetIndex = 0n
  private idSequence = 0

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

  constructor(options: FileTransferOptions) {
    this.options = options
    this.resumeStore = options.resumeStore ?? new Map()
    // 速度:1s 差值法(io_loop.rs:1048 update_jobs_status)
    this.speedTimer = window.setInterval(() => this.updateSpeeds(), 1000)
  }

  private log(message: string) {
    this.options.onLog?.(`[ft] ${message}`)
  }

  private nextId(): number {
    return ++this.idSequence
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
      deviceId: this.options.deviceId,
      streamId: this.options.streamId,
      ...fields,
    })
    // ft 通道 pkt_index 严格递增(render 按它排序投递)
    this.options.dc.send(packTlv(payload, this.packetIndex++))
  }

  // App.vue 把 ft_data_channel 的 onmessage 直接接到这里
  handleChannelMessage(buf: ArrayBuffer) {
    for (const payload of this.reassembler.feed(buf)) {
      let message: ReturnType<typeof decodeMessage>
      try {
        message = decodeMessage(payload)
      } catch (err) {
        this.log(`消息解码失败: ${String(err)}`)
        continue
      }
      try {
        this.dispatch(message)
      } catch (err) {
        this.log(`消息处理失败(type=${message.type}): ${String(err)}`)
      }
    }
  }

  private dispatch(message: ReturnType<typeof decodeMessage>) {
    const transferMessage = message as unknown as {
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
    if (transferMessage.type === MSG_TYPE_FILE_ACTION) {
      // 被控写侧对上传 digest 的自动决策回包(IsSame/NoSuchFile/skip)
      if (transferMessage.fileAction?.sendConfirm) this.onSendConfirm(transferMessage.fileAction.sendConfirm)
      return
    }
    if (transferMessage.type !== MSG_TYPE_FILE_RESPONSE || !transferMessage.fileResponse) return
    const fileResponse = transferMessage.fileResponse
    if (fileResponse.dir) this.onDir(fileResponse.dir)
    else if (fileResponse.block) this.onBlock(fileResponse.block)
    else if (fileResponse.error) this.onError(fileResponse.error)
    else if (fileResponse.done) this.onDone(fileResponse.done)
    else if (fileResponse.digest) void this.onDigest(fileResponse.digest)
    else if (fileResponse.emptyDirs) this.onEmptyDirs(fileResponse.emptyDirs)
  }

  // ---------- 作业状态 / 通知 ----------

  private touchJob(job: FtJob) {
    // 保留 speedBps:它由 updateSpeeds 写在 map 内的副本上,避免被作业侧旧对象覆盖;
    // 非 running 终态时以作业侧显式置 0 为准
    const previousJob = this.jobs.get(job.id)
    const speedBps = job.state === 'running' ? (previousJob?.speedBps ?? job.speedBps) : job.speedBps
    this.jobs.set(job.id, { ...job, speedBps })
    this.markJobsDirty()
  }

  private markJobsDirty() {
    if (this.jobsDirty) return
    this.jobsDirty = true
    queueMicrotask(() => {
      this.jobsDirty = false
      this.options.onJobsChanged?.(Array.from(this.jobs.values()))
    })
  }

  private emitJobs() {
    this.options.onJobsChanged?.(Array.from(this.jobs.values()))
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
      const previousSample = this.speedSamples.get(job.id)
      if (previousSample && now > previousSample.time) {
        job.speedBps = Math.max(0, Math.round(((job.finishedSize - previousSample.bytes) * 1000) / (now - previousSample.time)))
        changed = true
      }
      this.speedSamples.set(job.id, { time: now, bytes: job.finishedSize })
    }
    if (changed) this.emitJobs()
  }

  private speedSamples = new Map<number, { time: number; bytes: number }>()

  clearFinishedJobs() {
    for (const [id, job] of this.jobs) {
      if (job.state !== 'running' && job.state !== 'pending') {
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
        this.readDirQueue = this.readDirQueue.filter((pendingRequest) => pendingRequest.path !== path || pendingRequest.timer !== timer)
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
    const operationPromise = this.waitOp(id, `新建文件夹超时: ${path}`)
    this.sendAction({ create: { id, path } })
    return operationPromise
  }

  removeDir(path: string, recursive: boolean): Promise<void> {
    const id = this.nextId()
    const operationPromise = this.waitOp(id, `删除目录超时: ${path}`)
    this.sendAction({ removeDir: { id, path, recursive } })
    return operationPromise
  }

  removeFile(path: string): Promise<void> {
    const id = this.nextId()
    const operationPromise = this.waitOp(id, `删除文件超时: ${path}`)
    this.sendAction({ removeFile: { id, path, fileNum: 0 } })
    return operationPromise
  }

  rename(path: string, newName: string): Promise<void> {
    const id = this.nextId()
    const operationPromise = this.waitOp(id, `重命名超时: ${path}`)
    this.sendAction({ rename: { id, path, newName } })
    return operationPromise
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
      totalSize: items.reduce((totalSize, item) => totalSize + item.size, 0),
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
    const uploadState = this.uploadJobs.get(id)
    const job = this.jobs.get(id)
    if (!uploadState || !job) return
    uploadState.activated = true
    job.state = 'running'
    this.touchJob(job)
    // FileAction.receive:对端建写作业(fs.rs new_write)
    this.sendAction({
      receive: {
        id,
        path: uploadState.remoteTo,
        files: uploadState.items.map((item) => ({
          entryType: FT_TYPE_FILE,
          name: item.name,
          size: item.size,
          modifiedTime: item.modifiedTime,
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
    const uploadState = this.uploadJobs.get(id)
    const job = this.jobs.get(id)
    if (!uploadState || !job) return

    for (let itemIndex = 0; itemIndex < uploadState.items.length; itemIndex++) {
      if (uploadState.cancelled) return
      const item = uploadState.items[itemIndex]
      job.fileNum = itemIndex
      this.touchJob(job)

      // 逐文件 digest 握手(覆盖检测):必须先挂起确认等待再发送。
      // RTC 本机/局域网回包可以在 send() 返回前同步触发 onmessage；
      // 如果先发后挂 await，send_confirm 会成为丢失唤醒，作业永久停在 0 字节。
      const confirmation = await new Promise<{ skip: boolean; offset: number }>((resolve, reject) => {
        const timer = window.setTimeout(() => {
          if (uploadState.confirmWait?.fileNum === itemIndex) {
            uploadState.confirmWait = null
          }
          reject(new Error(`等待远端文件确认超时: ${item.name || uploadState.remoteTo}`))
        }, RESP_TIMEOUT_MS)
        const pendingConfirmation = { fileNum: itemIndex, resolve, reject, timer }
        uploadState.confirmWait = pendingConfirmation
        try {
          this.sendResponse({
            digest: {
              id,
              fileNum: itemIndex,
              lastModified: item.modifiedTime,
              fileSize: item.size,
              isResume: uploadState.isResume,
            },
          })
        } catch (err) {
          window.clearTimeout(timer)
          if (uploadState.confirmWait === pendingConfirmation) uploadState.confirmWait = null
          reject(err instanceof Error ? err : new Error(String(err)))
        }
      })
      uploadState.confirmWait = null
      if (uploadState.cancelled) return
      if (confirmation.skip) {
        job.skippedCount++
        job.finishedSize += item.size
        this.touchJob(job)
        continue
      }

      // 读侧按字节偏移定位(续传;offset_blk 名为块号实为字节偏移,plan §5.1)
      let readOffset = confirmation.offset
      if (readOffset > 0) {
        job.finishedSize += readOffset
        job.transferred += readOffset
      }
      while (readOffset < item.size) {
        if (uploadState.cancelled) return
        await this.waitSendBuffer()
        if (uploadState.cancelled) return
        const blockEnd = Math.min(readOffset + FT_BLOCK_SIZE, item.size)
        const rawBytes = new Uint8Array(await item.file.slice(readOffset, blockEnd).arrayBuffer())
        // 发送侧压缩:zlib deflate(与对端 miniz mz_compress2 格式一致);
        // 已压缩后缀或不划算时发原始块
        let data = rawBytes
        let compressed = false
        if (!isCompressedName(item.name)) {
          const compressedBytes = zlibSync(rawBytes, { level: 6 })
          if (compressedBytes.length > 0 && compressedBytes.length < rawBytes.length) {
            data = compressedBytes
            compressed = true
          }
        }
        this.sendResponse({ block: { id, fileNum: itemIndex, data, compressed } })
        job.finishedSize += rawBytes.length
        job.transferred += data.length
        this.markJobsDirty()
        readOffset = blockEnd
      }
      // EOF:发空数据块(旧 file_num),写侧靠后续块的新 file_num 推进(fs.rs:1001)
      this.sendResponse({ block: { id, fileNum: itemIndex, data: new Uint8Array(0), compressed: false } })
    }
    // 全部文件读完:作业完成(fs.rs handle_read_jobs -> new_done)
    this.sendResponse({ done: { id, fileNum: uploadState.items.length } })
    job.state = 'done'
    job.fileNum = uploadState.items.length
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
      currentFileNumber: -1,
      currentChunks: [],
      currentReceivedBytes: 0,
      currentFileSkipped: false,
      activated: false,
      cancelled: false,
    })
    this.emitJobs()
    this.maybeActivateNext()
    return job
  }

  private activateDownload(id: number) {
    const downloadState = this.downloadJobs.get(id)
    const job = this.jobs.get(id)
    if (!downloadState || !job) return
    downloadState.activated = true
    job.state = 'running'
    this.touchJob(job)
    // FileAction.send:请对端发送文件(对端建读作业,先回 dir 文件清单)
    this.sendAction({
      send: { id, path: downloadState.remoteFrom, includeHidden: false, fileNum: 0, fileType: 0 },
    })
  }

  // 下载文件清单到达(FileResponse.dir,connection.rs:5295 语义)
  private onDir(directoryResponse: { id: number; path: string; entries?: Array<Record<string, unknown>> }) {
    // 优先配对下载作业(对端回的作业文件列表)
    const downloadState = this.downloadJobs.get(directoryResponse.id)
    if (downloadState && !downloadState.gotDir) {
      downloadState.gotDir = true
      downloadState.files = (directoryResponse.entries ?? []).map((entry) => ({
        name: String(entry.name ?? ''),
        size: toNum(entry.size),
        mtime: toNum(entry.modifiedTime),
      }))
      const job = this.jobs.get(directoryResponse.id)
      if (job) {
        job.fileCount = downloadState.files.length
        job.totalSize = downloadState.files.reduce((totalSize, file) => totalSize + file.size, 0)
        this.touchJob(job)
      }
      if (downloadState.dirWait) {
        window.clearTimeout(downloadState.dirWait.timer)
        downloadState.dirWait.resolve()
        downloadState.dirWait = null
      }
      return
    }
    // ReadAllFiles 配对
    const pendingAll = this.pendingAllFiles.get(directoryResponse.id)
    if (pendingAll) {
      this.pendingAllFiles.delete(directoryResponse.id)
      window.clearTimeout(pendingAll.timer)
      const files = (directoryResponse.entries ?? []).map((entry) => ({
        type: toNum(entry.entryType),
        name: String(entry.name ?? ''),
        path: joinRemote(directoryResponse.path, String(entry.name ?? '')),
        size: toNum(entry.size),
        modifiedTime: toNum(entry.modifiedTime),
        isHidden: !!entry.isHidden,
      }))
      pendingAll.resolve(files as never)
      return
    }
    // read_dir FIFO(read_dir 无 id,回包 id=0;通道有序 + 对端单 worker 串行处理)
    const pending = this.readDirQueue.shift()
    if (pending) {
      window.clearTimeout(pending.timer)
      pending.resolve({
        path: directoryResponse.path || pending.path,
        files: (directoryResponse.entries ?? []).map((entry) => ({
          type: toNum(entry.entryType),
          name: String(entry.name ?? ''),
          path: joinRemote(directoryResponse.path, String(entry.name ?? '')),
          size: toNum(entry.size),
          modifiedTime: toNum(entry.modifiedTime),
          isHidden: !!entry.isHidden,
        })),
      })
      return
    }
    this.log(`收到无对应请求的 dir 响应 id=${directoryResponse.id} path=${directoryResponse.path}`)
  }

  // digest 到达:下载方向(写侧决策)与上传方向(读侧被报回冲突)两种
  private async onDigest(digestResponse: {
    id: number
    fileNum: number
    lastModified: unknown
    fileSize: unknown
    isUpload?: boolean
    isIdentical?: boolean
    transferredSize: unknown
    isResume?: boolean
  }) {
    if (digestResponse.isUpload) {
      // 上传方向:对端(写侧)报回它本地的同名文件情况(ui_cm_interface.rs:1116 语义;
      // 当前 render 引擎对冲突自动 skip 不走此分支,此处为完整性与未来升级实现)
      const uploadState = this.uploadJobs.get(digestResponse.id)
      const job = this.jobs.get(digestResponse.id)
      if (!uploadState || !job || !uploadState.confirmWait || uploadState.confirmWait.fileNum !== digestResponse.fileNum) return
      const item = uploadState.items[digestResponse.fileNum]
      const resumeBytes = digestResponse.isIdentical && digestResponse.isResume ? toNum(digestResponse.transferredSize) : 0
      let decision: OverwriteDecision
      const strategy = this.uploadStrategy
      if (resumeBytes > 0 && strategy !== 'skip') {
        decision = 'resume'
      } else if (strategy) {
        decision = strategy
      } else if (this.options.onOverwriteRequest) {
        decision = await this.options.onOverwriteRequest({
          jobId: digestResponse.id,
          fileNum: digestResponse.fileNum,
          path: joinRemote(uploadState.remoteTo, item?.name ?? ''),
          isUpload: true,
          isIdentical: !!digestResponse.isIdentical,
          remoteSize: toNum(digestResponse.fileSize),
          remoteMtime: toNum(digestResponse.lastModified),
          localSize: item?.size ?? -1,
          resumableBytes: resumeBytes,
        })
      } else {
        decision = 'skip'
      }
      const offset = decision === 'resume' ? resumeBytes : 0
      const skip = decision === 'skip'
      this.sendAction({ sendConfirm: { id: digestResponse.id, fileNum: digestResponse.fileNum, ...(skip ? { skip: true } : { offsetBlk: offset }) } })
      window.clearTimeout(uploadState.confirmWait.timer)
      uploadState.confirmWait.resolve({ skip, offset })
      return
    }

    // 下载方向:对端(读侧)报源文件 digest,本地做覆盖/续传决策(ft_engine.cpp:418)
    const downloadState = this.downloadJobs.get(digestResponse.id)
    const job = this.jobs.get(digestResponse.id)
    if (!downloadState || !job) return
    const entry = downloadState.files[digestResponse.fileNum]
    if (!entry) return
    const fileSize = toNum(digestResponse.fileSize)
    const lastModified = toNum(digestResponse.lastModified)
    const displayName = entry.name || job.displayName

    // 新文件的 digest 到达意味着上一文件已全部收完(通道有序),先收尾
    if (downloadState.currentFileNumber >= 0 && downloadState.currentFileNumber !== digestResponse.fileNum) {
      await this.finalizeCurrentFile(digestResponse.id)
      if (downloadState.cancelled || !this.downloadJobs.has(digestResponse.id)) return
    }

    // 会话内续传:内存里有同名同 size/mtime 的部分数据 -> 直接 offset 续传
    const resumeKey = `${downloadState.remoteFrom}\n${entry.name}`
    const cached = this.resumeStore.get(resumeKey)
    let offset = 0
    let skip = false
    if (cached && cached.size === fileSize && cached.mtime === lastModified && cached.data.length > 0 && cached.data.length < fileSize) {
      offset = cached.data.length
      downloadState.currentChunks = [cached.data]
      downloadState.currentReceivedBytes = cached.data.length
      this.resumeStore.delete(resumeKey)
      this.log(`续传: ${displayName} 从 ${offset} 字节继续`)
    } else {
      if (cached) this.resumeStore.delete(resumeKey) // 内容已变,丢弃旧缓存
      downloadState.currentChunks = []
      downloadState.currentReceivedBytes = 0
      // 本地已有文件探测(FS Access 模式):identical -> skip;不同 -> 弹框
      const probe = this.options.localFileProbe
        ? await this.options.localFileProbe(digestResponse.id, displayName)
        : null
      if (probe && probe.size === fileSize && probe.mtime === lastModified) {
        skip = true
      } else if (probe) {
        let decision: OverwriteDecision
        if (this.downloadStrategy) {
          decision = this.downloadStrategy
        } else if (this.options.onOverwriteRequest) {
          decision = await this.options.onOverwriteRequest({
            jobId: digestResponse.id,
            fileNum: digestResponse.fileNum,
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
    if (downloadState.cancelled) return
    downloadState.currentFileNumber = digestResponse.fileNum
    downloadState.currentFileSkipped = skip
    job.fileNum = digestResponse.fileNum
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
      sendConfirm: { id: digestResponse.id, fileNum: digestResponse.fileNum, ...(skip ? { skip: true } : { offsetBlk: offset }) },
    })
  }

  // 上传读侧收到对端写侧的确认(FileAction.send_confirm;render 自动决策或主控 UI 决策的回包)
  private onSendConfirm(confirmation: { id: number; fileNum: number; skip?: boolean; offsetBlk?: number }) {
    const uploadState = this.uploadJobs.get(confirmation.id)
    if (!uploadState || !uploadState.confirmWait) return
    if (uploadState.confirmWait.fileNum !== confirmation.fileNum) return // 非当前文件的 confirm 忽略(fs.rs:1157)
    window.clearTimeout(uploadState.confirmWait.timer)
    const skip = confirmation.skip === true
    const offset = skip ? 0 : toNum(confirmation.offsetBlk)
    uploadState.confirmWait.resolve({ skip, offset })
  }

  private onBlock(block: { id: number; fileNum: number; data?: Uint8Array; compressed?: boolean }) {
    const downloadState = this.downloadJobs.get(block.id)
    const job = this.jobs.get(block.id)
    if (!downloadState || !job || downloadState.cancelled) return
    // 块切到新文件:收尾上一文件(fs.rs:760 write 内 modify_time 语义)。
    // 正常流程在 onDigest 里已收尾,这里是防御性兜底(如对端跳过 digest 直发块)
    if (downloadState.currentFileNumber >= 0 && block.fileNum !== downloadState.currentFileNumber) {
      void this.finalizeCurrentFile(block.id)
      downloadState.currentFileNumber = block.fileNum
      job.fileNum = block.fileNum
    }
    const data = block.data
    if (!data || data.length === 0) return // EOF 空块
    let chunk: Uint8Array
    if (block.compressed) {
      try {
        chunk = unzlibSync(data)
      } catch (err) {
        this.failJob(block.id, `解压失败: ${err instanceof Error ? err.message : String(err)}`)
        return
      }
    } else {
      chunk = data
    }
    if (downloadState.currentFileSkipped) return // 防御:skip 的文件不应有块
    downloadState.currentChunks.push(chunk)
    downloadState.currentReceivedBytes += chunk.length
    job.finishedSize += chunk.length
    job.transferred += data.length
    this.markJobsDirty()
  }

  // 当前文件收齐:快照缓冲区后交付上层落盘;失败中断作业
  private async finalizeCurrentFile(id: number) {
    const downloadState = this.downloadJobs.get(id)
    const job = this.jobs.get(id)
    if (!downloadState || !job || downloadState.currentFileNumber < 0) return
    const fileNum = downloadState.currentFileNumber
    const skipped = downloadState.currentFileSkipped
    const chunks = downloadState.currentChunks
    const received = downloadState.currentReceivedBytes
    downloadState.currentFileNumber = -1
    downloadState.currentChunks = []
    downloadState.currentReceivedBytes = 0
    downloadState.currentFileSkipped = false
    if (skipped) return
    const entry = downloadState.files[fileNum]
    const name = entry?.name ?? ''
    const data = concatChunks(chunks, received)
    if (entry && entry.size > 0 && data.length !== entry.size) {
      this.failJob(id, `大小校验失败: ${name} ${data.length} != ${entry.size}`)
      return
    }
    try {
      await this.options.onFileDownloaded?.(id, {
        name: name || job.displayName,
        data,
        size: data.length,
        modifiedTime: entry?.mtime ?? 0,
      })
    } catch (err) {
      this.failJob(id, `写入本地失败: ${err instanceof Error ? err.message : String(err)}`)
    }
  }

  private onDone(completion: { id: number; fileNum: number }) {
    // 目录操作(create/remove/rename)的完成回包
    const pendingOperation = this.pendingOps.get(completion.id)
    if (pendingOperation) {
      this.pendingOps.delete(completion.id)
      window.clearTimeout(pendingOperation.timer)
      pendingOperation.resolve(undefined as never)
      return
    }
    // 下载作业完成
    const downloadState = this.downloadJobs.get(completion.id)
    const job = this.jobs.get(completion.id)
    if (!downloadState || !job) return
    void (async () => {
      if (downloadState.currentFileNumber >= 0 && !downloadState.cancelled) {
        await this.finalizeCurrentFile(completion.id)
      }
      // finalize 可能已把作业置为 error(落盘失败)
      const currentJob = this.jobs.get(completion.id)
      if (!currentJob || currentJob.state !== 'running') return
      currentJob.state = 'done'
      currentJob.speedBps = 0
      this.touchJob(currentJob)
      this.log(`下载完成: ${currentJob.displayName} (${currentJob.finishedSize} bytes, 跳过 ${currentJob.skippedCount})`)
      this.finishJob(completion.id)
    })()
  }

  private onError(errorResponse: { id: number; error: string; fileNum: number }) {
    const pendingOperation = this.pendingOps.get(errorResponse.id)
    if (pendingOperation) {
      this.pendingOps.delete(errorResponse.id)
      window.clearTimeout(pendingOperation.timer)
      pendingOperation.reject(new Error(errorResponse.error))
      return
    }
    const pendingAll = this.pendingAllFiles.get(errorResponse.id)
    if (pendingAll) {
      this.pendingAllFiles.delete(errorResponse.id)
      window.clearTimeout(pendingAll.timer)
      pendingAll.reject(new Error(errorResponse.error))
      return
    }
    if (this.jobs.has(errorResponse.id)) {
      this.failJob(errorResponse.id, errorResponse.error)
      return
    }
    this.log(`对端错误: id=${errorResponse.id} ${errorResponse.error}`)
  }

  private onEmptyDirs(response: { path: string; emptyDirs?: Array<{ path: string }> }) {
    const pending = this.pendingEmptyDirs.get(response.path)
    if (!pending) return
    this.pendingEmptyDirs.delete(response.path)
    window.clearTimeout(pending.timer)
    pending.resolve((response.emptyDirs ?? []).map((directory) => directory.path) as never)
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
    const uploadState = this.uploadJobs.get(id)
    if (uploadState) {
      uploadState.cancelled = true
      if (uploadState.confirmWait) window.clearTimeout(uploadState.confirmWait.timer)
      uploadState.confirmWait?.resolve({ skip: true, offset: 0 })
      uploadState.confirmWait = null
    }
    this.touchJob(job)
    this.log(`作业失败: ${job.displayName}: ${error}`)
    // 下载中断:保留已收部分进续传缓存(显式取消不清——那是 cancel 的路径)
    const downloadState = this.downloadJobs.get(id)
    if (downloadState) {
      downloadState.dirWait?.reject(new Error(error))
      downloadState.dirWait = null
      if (downloadState.currentFileNumber >= 0 && !downloadState.currentFileSkipped && downloadState.currentReceivedBytes > 0) {
        const entry = downloadState.files[downloadState.currentFileNumber]
        if (entry) {
          this.putResumeCache(`${downloadState.remoteFrom}\n${entry.name}`, {
            data: concatChunks(downloadState.currentChunks, downloadState.currentReceivedBytes),
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
    const uploadState = this.uploadJobs.get(id)
    const downloadState = this.downloadJobs.get(id)
    const activated = uploadState?.activated || downloadState?.activated
    if (uploadState) {
      uploadState.cancelled = true
      if (uploadState.confirmWait) window.clearTimeout(uploadState.confirmWait.timer)
      uploadState.confirmWait?.resolve({ skip: true, offset: 0 }) // 解开等待,runUpload 自行退出
      uploadState.confirmWait = null
    }
    if (downloadState) {
      downloadState.cancelled = true
      downloadState.dirWait?.reject(new Error('已取消'))
    }
    job.state = 'cancelled'
    job.speedBps = 0
    this.touchJob(job)
    if (activated) {
      this.sendAction({ cancel: { id } })
    }
    // 显式取消:清除该作业的续传缓存(对齐写侧 RemoveDownloadFile 语义)
    if (downloadState) {
      for (const file of downloadState.files) {
        this.resumeStore.delete(`${downloadState.remoteFrom}\n${file.name}`)
      }
    }
    this.log(`已取消: ${job.displayName}`)
    this.finishJob(id)
  }

  // "应用到全部"覆盖策略(UI 勾选后设置,后续冲突不再弹框)
  uploadStrategy: OverwriteDecision | null = null
  downloadStrategy: OverwriteDecision | null = null
  setOverwriteStrategy(direction: 'upload' | 'download', strategy: OverwriteDecision | null) {
    if (direction === 'upload') this.uploadStrategy = strategy
    else this.downloadStrategy = strategy
  }

  // ---------- 反压 ----------

  // datachannel 发送缓冲高水位等待(块级无 ack,这是唯一的流控)
  private waitSendBuffer(): Promise<void> {
    const dataChannel = this.options.dc
    if (dataChannel.readyState !== 'open') return Promise.reject(new Error('通道已断开'))
    if (dataChannel.bufferedAmount <= MAX_BUFFERED_BYTES) return Promise.resolve()
    return new Promise((resolve, reject) => {
      dataChannel.bufferedAmountLowThreshold = MAX_BUFFERED_BYTES / 2
      const onLow = () => {
        dataChannel.removeEventListener('bufferedamountlow', onLow)
        dataChannel.removeEventListener('close', onClose)
        resolve()
      }
      const onClose = () => {
        dataChannel.removeEventListener('bufferedamountlow', onLow)
        reject(new Error('通道已断开'))
      }
      dataChannel.addEventListener('bufferedamountlow', onLow)
      dataChannel.addEventListener('close', onClose, { once: true })
    })
  }

  // ---------- 续传缓存 ----------

  private putResumeCache(key: string, cacheEntry: { data: Uint8Array; size: number; mtime: number }) {
    let total = 0
    for (const storedEntry of this.resumeStore.values()) total += storedEntry.data.length
    // 超限:清最旧的(Map 迭代序 = 插入序)
    while (total + cacheEntry.data.length > RESUME_CACHE_MAX_BYTES && this.resumeStore.size > 0) {
      const oldest = this.resumeStore.keys().next().value
      if (oldest === undefined) break
      total -= this.resumeStore.get(oldest)?.data.length ?? 0
      this.resumeStore.delete(oldest)
    }
    if (cacheEntry.data.length <= RESUME_CACHE_MAX_BYTES) this.resumeStore.set(key, cacheEntry)
  }

  // ---------- 清理 ----------

  // 通道断开/页面清理:失败所有进行中作业与请求;下载已收部分保留续传缓存(断线语义)
  failAll(reason: string) {
    this.dead = true
    this.activeJobId = 0 // 先阻止 failJob -> finishJob 激活排队作业
    for (const pendingRequest of this.readDirQueue) {
      window.clearTimeout(pendingRequest.timer)
      pendingRequest.reject(new Error(reason))
    }
    this.readDirQueue = []
    for (const [id, pendingOperation] of this.pendingOps) {
      window.clearTimeout(pendingOperation.timer)
      pendingOperation.reject(new Error(reason))
      this.pendingOps.delete(id)
    }
    for (const [id, pendingRequest] of this.pendingAllFiles) {
      window.clearTimeout(pendingRequest.timer)
      pendingRequest.reject(new Error(reason))
      this.pendingAllFiles.delete(id)
    }
    for (const [path, pendingRequest] of this.pendingEmptyDirs) {
      window.clearTimeout(pendingRequest.timer)
      pendingRequest.reject(new Error(reason))
      this.pendingEmptyDirs.delete(path)
    }
    for (const id of Array.from(this.jobs.keys())) {
      const job = this.jobs.get(id)
      if (job && (job.state === 'running' || job.state === 'pending')) {
        const uploadState = this.uploadJobs.get(id)
        if (uploadState) {
          uploadState.cancelled = true
          if (uploadState.confirmWait) window.clearTimeout(uploadState.confirmWait.timer)
          uploadState.confirmWait?.resolve({ skip: true, offset: 0 })
          uploadState.confirmWait = null
        }
        const downloadState = this.downloadJobs.get(id)
        if (downloadState) {
          downloadState.cancelled = true
          downloadState.dirWait?.reject(new Error(reason))
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
  const combinedBytes = new Uint8Array(total)
  let writeOffset = 0
  for (const chunk of chunks) {
    combinedBytes.set(chunk, writeOffset)
    writeOffset += chunk.length
  }
  return combinedBytes
}
