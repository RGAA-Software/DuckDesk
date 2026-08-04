// 文件传输客户端:经 ft_data_channel 与 render 互传文件
// 协议对齐:
//   - src/gr_client/plugins/file_transfer_client/src/core/file_transmit_sdk.cc (C++ 控制端)
//   - src/gr_render/plugins/file_transfer/file_transmission_server/file_transmit_impl.cc (render 被控端)
// 要点:
//   - 每条消息 = NetTlvHeader + tc.Message;ft 通道 TLV pkt_index 必须严格递增(render 按它排序投递)
//   - render 侧 >128KB 的消息会分片(Begin/Center/End),接收经 TlvReassembler 重组
//   - 上传:逐块发送,index 从 0 连续;render 每 100 块回一次 ack(index%100==0),
//     并在收尾校验文件大小后回 kFileTransRespUpload —— 因此这里用滑动窗口(未 ack 块数 <= 150)
//     而不是逐块等 ack(逐块等会死锁),最终成败以 kFileTransRespUpload 为准
//   - 下载:render 按 4KB 块推流,客户端每收一块回 kFileTransDataPacketResponse,
//     render 在 index - acked >= 180 时会降速等待
import { packTlv, TlvReassembler } from './tlv'
import {
  encodeMessage,
  decodeMessage,
  MSG_TYPE_FILE_OPERATION_EVENT,
  MSG_TYPE_FILE_OPERATE_RESP_RENAME,
  MSG_TYPE_FILE_OPERATE_RESP_GET_FILE_LIST,
  MSG_TYPE_FILE_OPERATE_RESP_CREATE_NEW_FOLDER,
  MSG_TYPE_FILE_OPERATE_RESP_DEL,
  MSG_TYPE_FILE_TRANS_RESP_UPLOAD,
  MSG_TYPE_FILE_TRANS_RESP_DOWNLOAD,
  MSG_TYPE_FILE_TRANS_DATA_PACKET,
  MSG_TYPE_FILE_TRANS_DATA_PACKET_RESPONSE,
  MSG_TYPE_FILE_TRANS_SAVE_FILE_EXCEPTION,
  FT_OP_DEL,
  FT_OP_CREATE_NEW_FOLDER,
  FT_OP_RENAME,
  FT_OP_GET_FILES_LIST,
  FT_OP_DOWNLOAD,
  FT_DIR_UPLOAD,
  FT_STATE_TRANSMITTING,
  FT_STATE_END,
  FT_STATE_CANCEL,
  FT_SAVE_EX_CANCEL,
} from './proto'

export interface RemoteFileInfo {
  type: number // 0=disk 1=folder 2=file 3=deskFolder
  name: string
  path: string
  size: number
  date: number
}

export interface TransferTask {
  taskId: string
  direction: 'upload' | 'download'
  fileName: string
  total: number
  transferred: number
  // 即时传输速度(bytes/s),传输中按进度增量采样,结束后归零
  speedBps: number
  state: 'running' | 'done' | 'error' | 'cancelled'
  error?: string
}

export interface FileTransferOptions {
  dc: RTCDataChannel
  deviceId: string
  streamId: string
  onLog?: (msg: string) => void
  onTasksChanged?: (tasks: TransferTask[]) => void
}

const UPLOAD_CHUNK_SIZE = 64 * 1024
// 滑动窗口上限。render 每 100 块回一次 ack(file_transmit_impl.cc:88),
// 窗口必须 >= 100 否则会死锁(等 ack 时 ack 依赖的包还没发出去);对齐 C++ 客户端取 150(<180)
const UPLOAD_WINDOW = 150
// datachannel 发送缓冲水位:超过则等 bufferedamountlow 再继续,
// 无节制灌数据会把 SCTP 发送队列打满、拖垮整条 PeerConnection(实测 4MB 连发 ICE 直接 failed)
const MAX_BUFFERED_BYTES = 4 * 1024 * 1024
const RESP_TIMEOUT_MS = 30000

function toNum(v: unknown): number {
  if (typeof v === 'number') return v
  if (v && typeof (v as { toString(): string }).toString === 'function') {
    return Number((v as { toString(): string }).toString())
  }
  return 0
}

export async function sha256Hex(data: Uint8Array): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', data.slice().buffer)
  return Array.from(new Uint8Array(digest))
    .map((b) => b.toString(16).padStart(2, '0'))
    .join('')
}

export class FileTransferClient {
  private opts: FileTransferOptions
  private reassembler = new TlvReassembler()
  private pktIndex = 0n
  private operateSeq = 0
  private taskSeq = 0

  private tasks = new Map<string, TransferTask>()
  // 速度采样:taskId -> 上次采样的 {时间, 已传字节}
  private speedSamples = new Map<string, { time: number; bytes: number }>()

  private pendingLists = new Map<
    number,
    { resolve: (r: { path: string; files: RemoteFileInfo[] }) => void; reject: (e: Error) => void; timer: number }
  >()

  // 重命名/新建文件夹/删除的请求-响应配对(fileOperateSequence -> 回调)
  private pendingOps = new Map<
    number,
    {
      expectType: number
      resolve: (msg: ReturnType<typeof decodeMessage>) => void
      reject: (e: Error) => void
      timer: number
    }
  >()

  private uploads = new Map<
    string,
    {
      ackedIndex: number
      waiters: Array<() => void>
      done: { resolve: (target: string) => void; reject: (e: Error) => void }
      cancelled: boolean
      timer: number
    }
  >()

  private downloads = new Map<
    string,
    {
      remotePath: string
      fileSize: number
      chunks: Array<{ index: number; data: Uint8Array }>
      received: number
      done: { resolve: (r: { name: string; data: Uint8Array; sha256: string }) => void; reject: (e: Error) => void }
      timer: number
    }
  >()

  constructor(opts: FileTransferOptions) {
    this.opts = opts
  }

  private log(msg: string) {
    this.opts.onLog?.(`[ft] ${msg}`)
  }

  // ---------- 收发基础 ----------

  private sendMessage(fields: Record<string, unknown>) {
    const payload = encodeMessage({
      deviceId: this.opts.deviceId,
      streamId: this.opts.streamId,
      ...fields,
    })
    // ft 通道 pkt_index 严格递增(render 按它排序,rtc_data_channel.cpp:77-143)
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
    switch (msg.type) {
      case MSG_TYPE_FILE_OPERATE_RESP_GET_FILE_LIST:
        this.onFileList(msg)
        break
      case MSG_TYPE_FILE_OPERATE_RESP_RENAME:
      case MSG_TYPE_FILE_OPERATE_RESP_CREATE_NEW_FOLDER:
      case MSG_TYPE_FILE_OPERATE_RESP_DEL:
        this.onOperateResp(msg)
        break
      case MSG_TYPE_FILE_TRANS_DATA_PACKET:
        this.onDataPacket(msg)
        break
      case MSG_TYPE_FILE_TRANS_DATA_PACKET_RESPONSE:
        this.onDataPacketAck(msg)
        break
      case MSG_TYPE_FILE_TRANS_RESP_UPLOAD:
        this.onUploadResp(msg)
        break
      case MSG_TYPE_FILE_TRANS_RESP_DOWNLOAD:
        this.onDownloadResp(msg)
        break
      default:
        this.log(`未处理的消息类型: ${msg.type}`)
    }
  }

  // ---------- 任务状态 ----------

  private touchTask(task: TransferTask) {
    // 速度采样:与上次采样的字节增量 / 时间增量(简单即时值)
    const now = Date.now()
    if (task.state === 'running') {
      const prev = this.speedSamples.get(task.taskId)
      if (prev && now > prev.time) {
        task.speedBps = Math.max(0, Math.round(((task.transferred - prev.bytes) * 1000) / (now - prev.time)))
      } else if (!prev) {
        task.speedBps = 0
      }
      this.speedSamples.set(task.taskId, { time: now, bytes: task.transferred })
    } else {
      task.speedBps = 0
      this.speedSamples.delete(task.taskId)
    }
    this.tasks.set(task.taskId, { ...task })
    this.emitTasks()
  }

  private emitTasks() {
    this.opts.onTasksChanged?.(Array.from(this.tasks.values()))
  }

  getTasks(): TransferTask[] {
    return Array.from(this.tasks.values())
  }

  // 清除已结束(完成/失败/取消)的任务记录,传输中的保留
  clearFinishedTasks() {
    for (const [id, t] of this.tasks) {
      if (t.state !== 'running') {
        this.tasks.delete(id)
        this.speedSamples.delete(id)
      }
    }
    this.emitTasks()
  }

  // ---------- 列目录 ----------

  listDir(path: string): Promise<{ path: string; files: RemoteFileInfo[] }> {
    const seq = ++this.operateSeq
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.pendingLists.delete(seq)
        reject(new Error(`列目录超时: ${path}`))
      }, RESP_TIMEOUT_MS)
      this.pendingLists.set(seq, { resolve, reject, timer })
      this.sendMessage({
        type: MSG_TYPE_FILE_OPERATION_EVENT,
        fileOperateSequence: seq,
        fileOperateionsEvent: {
          operateType: FT_OP_GET_FILES_LIST,
          pathOfFilelist: path,
        },
      })
    })
  }

  private onFileList(msg: ReturnType<typeof decodeMessage>) {
    const seq = toNum(msg.fileOperateRespSequence)
    const pending = this.pendingLists.get(seq)
    if (!pending) {
      this.log(`收到无对应请求的目录响应 seq=${seq}`)
      return
    }
    this.pendingLists.delete(seq)
    window.clearTimeout(pending.timer)
    const resp = msg.fileOperateRespGetFileList
    if (!resp || !resp.ret) {
      pending.reject(new Error(resp?.msgOfError || '列目录失败'))
      return
    }
    pending.resolve({
      path: resp.path,
      files: (resp.fileInfos ?? []).map((f) => ({
        type: f.type,
        name: f.name,
        path: f.path,
        size: toNum(f.size),
        date: toNum(f.date),
      })),
    })
  }

  // ---------- 重命名 / 新建文件夹 / 删除 ----------
  // 与 listDir 同一套 fileOperateSequence 请求-响应配对,只是响应消息类型不同

  private sendOperate(
    expectType: number,
    event: Record<string, unknown>,
    timeoutError: string,
  ): Promise<ReturnType<typeof decodeMessage>> {
    const seq = ++this.operateSeq
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.pendingOps.delete(seq)
        reject(new Error(timeoutError))
      }, RESP_TIMEOUT_MS)
      this.pendingOps.set(seq, { expectType, resolve, reject, timer })
      this.sendMessage({
        type: MSG_TYPE_FILE_OPERATION_EVENT,
        fileOperateSequence: seq,
        fileOperateionsEvent: event,
      })
    })
  }

  private onOperateResp(msg: ReturnType<typeof decodeMessage>) {
    const seq = toNum(msg.fileOperateRespSequence)
    const pending = this.pendingOps.get(seq)
    if (!pending) {
      this.log(`收到无对应请求的操作响应 type=${msg.type} seq=${seq}`)
      return
    }
    if (msg.type !== pending.expectType) {
      this.log(`操作响应类型不匹配 seq=${seq} expect=${pending.expectType} got=${msg.type}`)
      return
    }
    this.pendingOps.delete(seq)
    window.clearTimeout(pending.timer)
    pending.resolve(msg)
  }

  // 在 parentPath 下新建文件夹(名字由 render 自动生成: 新建文件夹/新建文件夹(2)...)
  async createFolder(parentPath: string): Promise<string> {
    const msg = await this.sendOperate(
      MSG_TYPE_FILE_OPERATE_RESP_CREATE_NEW_FOLDER,
      { operateType: FT_OP_CREATE_NEW_FOLDER, pathOfCreateNewFolder: parentPath },
      `新建文件夹超时: ${parentPath}`,
    )
    const resp = msg.fileOperateRespCreateNewFolder
    if (!resp || !resp.ret) {
      throw new Error(resp?.msgOfError || '新建文件夹失败')
    }
    this.log(`新建文件夹: ${resp.pathOfNewCreated}`)
    return resp.pathOfNewCreated
  }

  async deleteFile(path: string): Promise<void> {
    const msg = await this.sendOperate(
      MSG_TYPE_FILE_OPERATE_RESP_DEL,
      { operateType: FT_OP_DEL, pathsOfDel: [path] },
      `删除超时: ${path}`,
    )
    const resp = msg.fileOperateRespDel
    if (!resp || !resp.ret || (resp.pathsOfNoDel?.length ?? 0) > 0) {
      throw new Error(resp?.msgOfError || '删除失败')
    }
    this.log(`删除成功: ${path}`)
  }

  // newName 仅文件名(不含路径),render 在同目录下重命名
  async renameFile(path: string, newName: string): Promise<string> {
    const msg = await this.sendOperate(
      MSG_TYPE_FILE_OPERATE_RESP_RENAME,
      { operateType: FT_OP_RENAME, pathOfRename: path, nameOfRename: newName },
      `重命名超时: ${path}`,
    )
    const resp = msg.fileOperateRespRename
    if (!resp || !resp.ret) {
      throw new Error(resp?.msgOfError || '重命名失败')
    }
    this.log(`重命名: ${resp.pathOfOld} -> ${resp.pathOfNew}`)
    return resp.pathOfNew
  }

  // ---------- 上传 ----------

  private newTaskId(prefix: string): string {
    return `${prefix}-${Date.now()}-${++this.taskSeq}`
  }

  // targetDir 以 / 或 \ 结尾均可;目标路径 = targetDir + 文件名
  async upload(file: File, targetDir: string): Promise<{ taskId: string; targetFilePath: string }> {
    const taskId = this.newTaskId('up')
    const sep = targetDir.includes('\\') ? '\\' : '/'
    const targetFilePath = targetDir.replace(/[\\/]+$/, '') + sep + file.name

    const task: TransferTask = {
      taskId,
      direction: 'upload',
      fileName: file.name,
      total: file.size,
      transferred: 0,
      speedBps: 0,
      state: 'running',
    }
    this.touchTask(task)

    const result = await new Promise<string>((resolve, reject) => {
      const totalChunks = Math.max(1, Math.ceil(file.size / UPLOAD_CHUNK_SIZE))
      const timer = window.setTimeout(() => {
        this.failUpload(taskId, '上传超时')
      }, Math.max(RESP_TIMEOUT_MS * 2, totalChunks * 1000))
      this.uploads.set(taskId, {
        ackedIndex: -1,
        waiters: [],
        done: { resolve, reject },
        cancelled: false,
        timer,
      })
      void this.runUpload(taskId, file, targetFilePath, task).catch((err) => {
        this.failUpload(taskId, err instanceof Error ? err.message : String(err))
      })
    })
    return { taskId, targetFilePath: result }
  }

  // datachannel 发送缓冲高水位等待:防止无节制发送撑爆 SCTP 队列
  private waitSendBuffer(): Promise<void> {
    const dc = this.opts.dc
    if (dc.bufferedAmount <= MAX_BUFFERED_BYTES) return Promise.resolve()
    return new Promise((resolve) => {
      dc.bufferedAmountLowThreshold = MAX_BUFFERED_BYTES / 2
      const onLow = () => {
        dc.removeEventListener('bufferedamountlow', onLow)
        resolve()
      }
      dc.addEventListener('bufferedamountlow', onLow)
    })
  }

  private async runUpload(taskId: string, file: File, targetFilePath: string, task: TransferTask) {
    const totalChunks = Math.max(1, Math.ceil(file.size / UPLOAD_CHUNK_SIZE))
    for (let index = 0; index < totalChunks; index++) {
      const up = this.uploads.get(taskId)
      if (!up) return // 已被失败/完成回调清掉
      if (up.cancelled) return
      // 滑动窗口:未 ack 块数达到上限时等 ack
      await this.waitUploadWindow(taskId, index)
      // 发送缓冲水位控制
      await this.waitSendBuffer()
      const isLast = index === totalChunks - 1
      const data = new Uint8Array(await file.slice(index * UPLOAD_CHUNK_SIZE, (index + 1) * UPLOAD_CHUNK_SIZE).arrayBuffer())
      this.sendMessage({
        type: MSG_TYPE_FILE_TRANS_DATA_PACKET,
        fileTransDataPacket: {
          transmitDirection: FT_DIR_UPLOAD,
          srcFilePath: file.name,
          targetFilePath,
          fileSize: file.size,
          taskId,
          index,
          transmitState: isLast ? FT_STATE_END : FT_STATE_TRANSMITTING,
          data,
        },
      })
      task.transferred = Math.min(file.size, (index + 1) * UPLOAD_CHUNK_SIZE)
      this.touchTask(task)
    }
  }

  private waitUploadWindow(taskId: string, nextIndex: number): Promise<void> {
    const up = this.uploads.get(taskId)
    if (!up) return Promise.reject(new Error('上传任务已结束'))
    if (nextIndex - up.ackedIndex <= UPLOAD_WINDOW) return Promise.resolve()
    return new Promise((resolve) => {
      up.waiters.push(resolve)
    })
  }

  private onDataPacketAck(msg: ReturnType<typeof decodeMessage>) {
    const resp = msg.fileTransDataPacketResponse
    if (!resp) return
    const up = this.uploads.get(resp.taskId)
    if (!up) return
    up.ackedIndex = Math.max(up.ackedIndex, toNum(resp.index))
    const waiters = up.waiters
    up.waiters = []
    for (const w of waiters) w()
  }

  private onUploadResp(msg: ReturnType<typeof decodeMessage>) {
    const resp = msg.fileTransRespUpload
    if (!resp) return
    const up = this.uploads.get(resp.taskId)
    const task = this.tasks.get(resp.taskId)
    this.uploads.delete(resp.taskId)
    if (up) {
      window.clearTimeout(up.timer)
      const waiters = up.waiters
      up.waiters = []
      for (const w of waiters) w()
    }
    if (resp.res) {
      if (task) this.touchTask({ ...task, state: 'done', transferred: task.total })
      this.log(`上传完成: ${resp.targetFilePath}`)
      up?.done.resolve(resp.targetFilePath)
    } else {
      const err = `上传失败, error_cause=${resp.errorCause}`
      if (task) this.touchTask({ ...task, state: 'error', error: err })
      this.log(`${err}: ${resp.targetFilePath}`)
      up?.done.reject(new Error(err))
    }
  }

  private failUpload(taskId: string, error: string) {
    const up = this.uploads.get(taskId)
    const task = this.tasks.get(taskId)
    this.uploads.delete(taskId)
    if (up) {
      window.clearTimeout(up.timer)
      const waiters = up.waiters
      up.waiters = []
      for (const w of waiters) w()
    }
    if (task) this.touchTask({ ...task, state: 'error', error })
    up?.done.reject(new Error(error))
  }

  // 通道断开/页面清理时,结束所有进行中的请求与任务
  failAll(reason: string) {
    for (const [seq, p] of this.pendingLists) {
      window.clearTimeout(p.timer)
      p.reject(new Error(reason))
      this.pendingLists.delete(seq)
    }
    for (const [seq, p] of this.pendingOps) {
      window.clearTimeout(p.timer)
      p.reject(new Error(reason))
      this.pendingOps.delete(seq)
    }
    for (const taskId of Array.from(this.uploads.keys())) {
      this.failUpload(taskId, reason)
    }
    for (const taskId of Array.from(this.downloads.keys())) {
      this.failDownload(taskId, reason)
    }
    this.speedSamples.clear()
  }

  // ---------- 下载 ----------

  // 下载远端文件,resolve 出完整内容与 sha256(调用方决定怎么落盘)
  download(
    remotePath: string,
  ): Promise<{ taskId: string; name: string; size: number; sha256: string; data: Uint8Array }> {
    const taskId = this.newTaskId('down')
    const name = remotePath.split(/[\\/]/).pop() || 'download.bin'

    this.touchTask({
      taskId,
      direction: 'download',
      fileName: name,
      total: 0,
      transferred: 0,
      speedBps: 0,
      state: 'running',
    })

    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.failDownload(taskId, '下载超时')
      }, RESP_TIMEOUT_MS * 20)
      this.downloads.set(taskId, {
        remotePath,
        fileSize: 0,
        chunks: [],
        received: 0,
        timer,
        done: {
          resolve: (r) => resolve({ taskId, name: r.name, size: r.data.length, sha256: r.sha256, data: r.data }),
          reject,
        },
      })
      this.sendMessage({
        type: MSG_TYPE_FILE_OPERATION_EVENT,
        fileOperateionsEvent: {
          operateType: FT_OP_DOWNLOAD,
          pathOfDownload: remotePath,
          pathOfSave: name,
          taskId,
        },
      })
    })
  }

  private onDataPacket(msg: ReturnType<typeof decodeMessage>) {
    const pkt = msg.fileTransDataPacket
    if (!pkt) return
    // 上传方向的包是发给 render 的,这里只会收到下载方向
    const dl = this.downloads.get(pkt.taskId)
    if (!dl) {
      this.log(`收到未知任务的下载包 task=${pkt.taskId}`)
      return
    }
    const index = toNum(pkt.index)
    window.clearTimeout(dl.timer)
    dl.timer = window.setTimeout(() => this.failDownload(pkt.taskId, '下载超时'), RESP_TIMEOUT_MS * 2)

    if (pkt.data && pkt.data.length > 0) {
      dl.chunks.push({ index, data: pkt.data })
      dl.received += pkt.data.length
    }
    dl.fileSize = toNum(pkt.fileSize)

    // 每块都回 ack(render 侧 index - acked >= 180 会降速等待)
    this.sendMessage({
      type: MSG_TYPE_FILE_TRANS_DATA_PACKET_RESPONSE,
      fileTransDataPacketResponse: { taskId: pkt.taskId, index },
    })

    const task = this.tasks.get(pkt.taskId)
    if (task) this.touchTask({ ...task, total: dl.fileSize, transferred: dl.received })

    if (pkt.transmitState === FT_STATE_END) {
      void this.finishDownload(pkt.taskId)
    } else if (pkt.transmitState !== FT_STATE_TRANSMITTING) {
      this.failDownload(pkt.taskId, `下载中断, transmit_state=${pkt.transmitState}`)
    }
  }

  private async finishDownload(taskId: string) {
    const dl = this.downloads.get(taskId)
    if (!dl) return
    dl.chunks.sort((a, b) => a.index - b.index)
    const data = new Uint8Array(dl.received)
    let offset = 0
    for (const c of dl.chunks) {
      data.set(c.data, offset)
      offset += c.data.length
    }
    if (dl.fileSize > 0 && data.length !== dl.fileSize) {
      this.failDownload(taskId, `大小校验失败: ${data.length} != ${dl.fileSize}`)
      return
    }
    const sha256 = await sha256Hex(data)
    const name = dl.remotePath.split(/[\\/]/).pop() || 'download.bin'
    const task = this.tasks.get(taskId)
    if (task) this.touchTask({ ...task, state: 'done', total: data.length, transferred: data.length })
    this.log(`下载完成: ${name} (${data.length} bytes, sha256=${sha256.slice(0, 16)}...)`)
    this.cleanupDownload(taskId)
    dl.done.resolve({ name, data, sha256 })
  }

  private failDownload(taskId: string, error: string) {
    const dl = this.downloads.get(taskId)
    const task = this.tasks.get(taskId)
    if (task) this.touchTask({ ...task, state: 'error', error })
    this.log(`下载失败: ${error}`)
    this.cleanupDownload(taskId)
    dl?.done.reject(new Error(error))
  }

  private cleanupDownload(taskId: string) {
    const dl = this.downloads.get(taskId)
    if (dl) {
      window.clearTimeout(dl.timer)
      this.downloads.delete(taskId)
    }
  }

  private onDownloadResp(msg: ReturnType<typeof decodeMessage>) {
    const resp = msg.fileTransRespDownload
    if (!resp) return
    // 该消息只在出错时下发(file_transmit_impl.cc:248)
    this.failDownload(resp.taskId, `render 下载错误, error_cause=${resp.errorCause}`)
  }

  // ---------- 取消 ----------

  cancel(taskId: string) {
    const task = this.tasks.get(taskId)
    if (!task || task.state !== 'running') return
    if (task.direction === 'upload') {
      const up = this.uploads.get(taskId)
      if (up) {
        up.cancelled = true
        // 发 kCancel 状态的包通知 render 收尾(file_transmit_impl.cc:188)
        this.sendMessage({
          type: MSG_TYPE_FILE_TRANS_DATA_PACKET,
          fileTransDataPacket: {
            transmitDirection: FT_DIR_UPLOAD,
            taskId,
            index: Math.max(0, up.ackedIndex + 1),
            transmitState: FT_STATE_CANCEL,
          },
        })
        up.done.reject(new Error('已取消'))
        window.clearTimeout(up.timer)
        const waiters = up.waiters
        up.waiters = []
        for (const w of waiters) w()
        this.uploads.delete(taskId)
      }
      this.touchTask({ ...task, state: 'cancelled' })
    } else {
      // 下载取消:对齐 C++ 端 SendSaveFileExceptionMessage(kCancel)
      this.sendMessage({
        type: MSG_TYPE_FILE_TRANS_SAVE_FILE_EXCEPTION,
        fileTransSaveFileException: {
          errorCause: FT_SAVE_EX_CANCEL,
          taskId,
          srcFilePath: task.fileName,
          targetFilePath: task.fileName,
        },
      })
      const dl = this.downloads.get(taskId)
      this.cleanupDownload(taskId)
      this.touchTask({ ...task, state: 'cancelled' })
      dl?.done.reject(new Error('已取消'))
    }
  }
}
