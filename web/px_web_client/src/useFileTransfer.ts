// 文件传输状态与操作(ft_data_channel):App.vue 与 FileTransferWindow.vue 之间的 composable
// 协议引擎在 rtc/file_transfer.ts(FileTransferClient,rustdesk 语义);
// 这里负责:通道生命周期、远端目录浏览状态、上传展开、下载落点路由、覆盖确认路由
import { ref } from 'vue'
import {
  FileTransferClient,
  joinRemote,
  parentRemote,
  FT_TYPE_DIR,
  FT_TYPE_DIR_LINK,
  FT_TYPE_DRIVE,
  FT_TYPE_FILE,
} from './rtc/file_transfer'
import type {
  FtJob,
  OverwriteDecision,
  OverwriteRequest,
  RemoteFileInfo,
  UploadFileItem,
} from './rtc/file_transfer'
import type { FsDirHandle, FsFileHandle } from './fs_access'
import { ensureDir, writeFile } from './fs_access'
import { buildZip } from './zip'

export function isRemoteDir(t: number): boolean {
  return t === FT_TYPE_DIR || t === FT_TYPE_DIR_LINK || t === FT_TYPE_DRIVE
}
export function isRemoteFile(t: number): boolean {
  return t === FT_TYPE_FILE
}

// 下载落点:FS Access 目录(直接写盘)、浏览器保存(降级)、内存(CDP 测试钩子)
export interface DownloadSink {
  type: 'fs' | 'browser' | 'memory'
  root?: FsDirHandle // type=fs
  // 下载目录时的顶层目录名(浏览器降级模式打包 zip 用)
  topName?: string
}

interface MemoryFile {
  name: string
  data: Uint8Array
  size: number
  modifiedTime: number
}

interface SinkState {
  sink: DownloadSink
  zipEntries: Array<{ name: string; data: Uint8Array }>
  memory?: {
    files: MemoryFile[]
    resolve: (files: MemoryFile[]) => void
    reject: (e: Error) => void
  }
}

export function useFileTransfer() {
  let ftClient: FileTransferClient | null = null
  let logFn: (msg: string) => void = () => {}
  // 会话内下载续传缓存:跨断线重连保留(新 client 复用),刷新页面即丢(plan §2 阶段 4.3)
  const resumeStore = new Map<string, { data: Uint8Array; size: number; mtime: number }>()
  const sinks = new Map<number, SinkState>()

  const ftReady = ref(false)
  const ftPath = ref('/')
  const ftFiles = ref<RemoteFileInfo[]>([])
  const ftLoading = ref(false)
  const ftError = ref('')
  const ftJobs = ref<FtJob[]>([])

  // 覆盖确认弹框由 Window 组件提供(赋值后即可用);未赋值时按引擎默认决策
  let overwriteHandler: ((req: OverwriteRequest) => Promise<OverwriteDecision>) | null = null
  function setOverwriteHandler(h: typeof overwriteHandler) {
    overwriteHandler = h
  }

  function log(msg: string) {
    logFn(msg)
  }

  // ft_data_channel onopen 时调用
  function initFt(dc: RTCDataChannel, deviceId: string, streamId: string, onLog: (msg: string) => void) {
    logFn = onLog
    ftClient = new FileTransferClient({
      dc,
      deviceId,
      streamId,
      onLog,
      resumeStore,
      onJobsChanged: (jobs) => {
        // 作业终结时收尾:内存暂存 resolve / 浏览器降级 zip 打包
        for (const j of jobs) {
          const s = sinks.get(j.id)
          if (s && (j.state === 'done' || j.state === 'error' || j.state === 'cancelled')) {
            sinks.delete(j.id)
            if (s.memory) {
              if (j.state === 'done') s.memory.resolve(s.memory.files)
              else s.memory.reject(new Error(j.error || j.state))
            } else if (s.sink.type === 'browser' && s.zipEntries.length > 0 && j.state === 'done') {
              saveBlob(`${s.sink.topName ?? 'download'}.zip`, buildZip(s.zipEntries))
              log(`文件夹已打包下载: ${s.sink.topName}.zip (${s.zipEntries.length} 项)`)
            }
          }
        }
        ftJobs.value = jobs
      },
      onOverwriteRequest: (req) => {
        if (overwriteHandler) return overwriteHandler(req)
        return Promise.resolve(req.isUpload ? ('skip' as const) : ('overwrite' as const))
      },
      onFileDownloaded: (jobId, file) => deliverFile(jobId, file),
      localFileProbe: (jobId, name) => probeLocal(jobId, name),
    })
    ftReady.value = true
    log('文件传输通道已就绪')
    void refresh('/')
  }

  // 连接断开/清理时调用:失败所有进行中任务并复位状态
  function resetFt(reason: string) {
    ftClient?.failAll(reason)
    ftClient = null
    sinks.clear()
    ftReady.value = false
    ftJobs.value = []
  }

  // App.vue 把 ft_data_channel 的 onmessage 转发到这里
  function handleChannelMessage(buf: ArrayBuffer) {
    ftClient?.handleChannelMessage(buf)
  }

  // ---------- 远端目录浏览 ----------

  async function refresh(path?: string) {
    if (!ftClient) return
    const target = path ?? ftPath.value
    ftLoading.value = true
    ftError.value = ''
    try {
      const result = await ftClient.listDir(target)
      ftPath.value = result.path || target
      ftFiles.value = result.files
    } catch (err) {
      ftError.value = err instanceof Error ? err.message : String(err)
    } finally {
      ftLoading.value = false
    }
  }

  function enter(item: RemoteFileInfo) {
    if (!isRemoteDir(item.type)) return
    // 盘符 "C:" 规范化为 "C:/"(std::filesystem 对裸盘符是驱动器相对路径,列目录会错)
    void refresh(/^[A-Za-z]:$/.test(item.name) && (ftPath.value === '/' || !ftPath.value) ? `${item.name}/` : item.path)
  }

  function up() {
    void refresh(parentRemote(ftPath.value))
  }

  // ---------- 上传 ----------

  // 递归展开本地目录句柄为上传条目(name 相对顶层目录)
  async function expandDir(dir: FsDirHandle, prefix: string, out: UploadFileItem[]): Promise<void> {
    for await (const handle of dir.values()) {
      const rel = prefix ? `${prefix}/${handle.name}` : handle.name
      if (handle.kind === 'file') {
        const file = await (handle as FsFileHandle).getFile()
        out.push({ name: rel, file, size: file.size, modifiedTime: Math.floor(file.lastModified / 1000) })
      } else {
        await expandDir(handle as FsDirHandle, rel, out)
      }
    }
  }

  // 收集空目录(上传前在远端批量建目录,rustdesk flutter 同款流程)
  async function collectEmptyDirs(dir: FsDirHandle, prefix: string, out: string[]): Promise<void> {
    let hasAny = false
    const subdirs: Array<[FsDirHandle, string]> = []
    for await (const handle of dir.values()) {
      hasAny = true
      if (handle.kind === 'directory') {
        const rel = prefix ? `${prefix}/${handle.name}` : handle.name
        subdirs.push([handle as FsDirHandle, rel])
      }
    }
    if (!hasAny && prefix) out.push(prefix)
    for (const [h, rel] of subdirs) await collectEmptyDirs(h, rel, out)
  }

  // 上传本地文件到远端目录(remoteDir 为当前远端目录;目标全路径 = remoteDir/name)
  function uploadFile(file: File, remoteDir?: string): FtJob {
    if (!ftClient) throw new Error('文件传输未就绪')
    const dir = remoteDir ?? ftPath.value
    const item: UploadFileItem = {
      name: '', // 单文件:receive.path 已含文件名,条目名留空(rustdesk 约定)
      file,
      size: file.size,
      modifiedTime: Math.floor(file.lastModified / 1000),
    }
    return ftClient.upload([item], joinRemote(dir, file.name), file.name)
  }

  // 递归上传本地文件夹:先批量建空目录,再投递整作业
  async function uploadFolder(dir: FsDirHandle, remoteDir?: string): Promise<FtJob> {
    if (!ftClient) throw new Error('文件传输未就绪')
    const parent = remoteDir ?? ftPath.value
    const remoteTo = joinRemote(parent, dir.name)
    const items: UploadFileItem[] = []
    await expandDir(dir, '', items)
    const emptyDirs: string[] = []
    await collectEmptyDirs(dir, '', emptyDirs)
    // 顶层目录本身先建(create_directories 幂等;空文件夹场景只有它)
    await ftClient.createDir(remoteTo)
    for (const rel of emptyDirs) {
      await ftClient.createDir(joinRemote(remoteTo, rel))
    }
    return ftClient.upload(items, remoteTo, dir.name)
  }

  // ---------- 下载 ----------

  // 投递下载作业;sink 决定落点(FS Access 写盘 / 浏览器保存)
  function downloadRemote(item: RemoteFileInfo, sink: DownloadSink): FtJob {
    if (!ftClient) throw new Error('文件传输未就绪')
    const job = ftClient.download(item.path, item.name)
    const isDir = isRemoteDir(item.type)
    sinks.set(job.id, { sink: isDir ? { ...sink, topName: sink.topName ?? item.name } : sink, zipEntries: [] })
    if (isDir) {
      // 空目录还原:readEmptyDirs 回包后在作业完成时补建(失败不阻断主流程)
      void ftClient
        .readEmptyDirs(item.path)
        .then((dirs) => {
          const s = sinks.get(job.id)
          if (!s) return
          for (const p of dirs) s.zipEntries.push({ name: `${normalizeEmptyDir(item, p)}/`, data: new Uint8Array(0) })
          if (s.sink.type === 'fs' && s.sink.root) {
            const root = s.sink.root
            for (const p of dirs) {
              void ensureDir(root, [item.name, ...normalizeEmptyDir(item, p).split('/')])
            }
          }
        })
        .catch(() => {})
    }
    return job
  }

  // readEmptyDirs 回包:嵌套空目录是相对路径,顶层自身为空时是全路径 —— 统一为相对顶层的名
  function normalizeEmptyDir(item: RemoteFileInfo, p: string): string {
    const base = item.path.replace(/[\\/]+$/, '').replace(/\\/g, '/')
    const rel = p.replace(/\\/g, '/')
    if (rel.toLowerCase().startsWith(base.toLowerCase() + '/')) return rel.slice(base.length + 1)
    if (rel.toLowerCase() === base.toLowerCase()) return ''
    return rel
  }

  // 下载文件交付:按 sink 写盘/暂存;目录下载在本地也落在顶层同名目录下
  async function deliverFile(
    jobId: number,
    file: { name: string; data: Uint8Array; size: number; modifiedTime: number },
  ) {
    const s = sinks.get(jobId)
    if (!s) return
    const localName = s.sink.topName ? `${s.sink.topName}/${file.name}` : file.name
    if (s.memory) {
      s.memory.files.push({ name: file.name, data: file.data, size: file.size, modifiedTime: file.modifiedTime })
      return
    }
    if (s.sink.type === 'fs' && s.sink.root) {
      const segs = localName.split('/')
      const dir = await ensureDir(s.sink.root, segs.slice(0, -1))
      await writeFile(dir, segs[segs.length - 1], file.data)
    } else if (s.sink.topName) {
      // 目录下载:攒起来作业完成后打包 zip
      s.zipEntries.push({ name: localName, data: file.data })
    } else {
      // 单文件:直接浏览器保存
      saveBlob(file.name, file.data)
    }
  }

  // 本地同名探测(FS Access 模式做 identical/覆盖决策;浏览器降级模式无本地视图,返回 null)
  async function probeLocal(jobId: number, name: string): Promise<{ size: number; mtime: number } | null> {
    const s = sinks.get(jobId)
    if (!s || s.sink.type !== 'fs' || !s.sink.root) return null
    try {
      const localName = s.sink.topName ? `${s.sink.topName}/${name}` : name
      const segs = localName.split('/')
      let dir = s.sink.root
      for (const seg of segs.slice(0, -1)) {
        dir = await dir.getDirectoryHandle(seg)
      }
      const fh = await dir.getFileHandle(segs[segs.length - 1])
      const f = await fh.getFile()
      return { size: f.size, mtime: Math.floor(f.lastModified / 1000) }
    } catch {
      return null
    }
  }

  function saveBlob(name: string, data: Uint8Array) {
    const url = URL.createObjectURL(new Blob([data.slice().buffer]))
    const a = document.createElement('a')
    a.href = url
    a.download = name
    a.click()
    URL.revokeObjectURL(url)
  }

  // CDP/调试钩子用:下载远端文件到内存,作业完成时 resolve 全部文件
  function downloadToMemory(
    remotePath: string,
  ): Promise<Array<{ name: string; data: Uint8Array; size: number; modifiedTime: number }>> {
    const client = ftClient
    if (!client) return Promise.reject(new Error('文件传输未就绪'))
    const name = remotePath.split(/[\\/]/).pop() || 'download.bin'
    const job = client.download(remotePath, name)
    return new Promise((resolve, reject) => {
      sinks.set(job.id, { sink: { type: 'memory' }, zipEntries: [], memory: { files: [], resolve, reject } })
    })
  }

  // ---------- 作业操作 ----------

  function cancelJob(job: FtJob) {
    ftClient?.cancel(job.id)
  }

  function clearFinished() {
    ftClient?.clearFinishedJobs()
  }

  function setOverwriteStrategy(direction: 'upload' | 'download', s: OverwriteDecision | null) {
    ftClient?.setOverwriteStrategy(direction, s)
  }

  // ---------- 远端目录工具 ----------

  async function createFolder(name: string) {
    if (!ftClient) return
    await ftClient.createDir(joinRemote(ftPath.value, name))
    await refresh()
  }

  async function renameRemote(item: RemoteFileInfo, newName: string) {
    if (!ftClient) return
    await ftClient.rename(item.path, newName)
    await refresh()
  }

  // 删除:目录走递归删除(递归确认由 UI 弹框负责)
  async function deleteRemote(item: RemoteFileInfo) {
    if (!ftClient) return
    if (isRemoteDir(item.type)) {
      await ftClient.removeDir(item.path, true)
    } else {
      await ftClient.removeFile(item.path)
    }
    await refresh()
  }

  function fmtSize(size: number): string {
    if (size < 1024) return `${size} B`
    if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KB`
    if (size < 1024 * 1024 * 1024) return `${(size / 1024 / 1024).toFixed(1)} MB`
    return `${(size / 1024 / 1024 / 1024).toFixed(2)} GB`
  }

  return {
    client: () => ftClient,
    ftReady,
    ftPath,
    ftFiles,
    ftLoading,
    ftError,
    ftJobs,
    initFt,
    resetFt,
    handleChannelMessage,
    setOverwriteHandler,
    refresh,
    enter,
    up,
    expandDir,
    uploadFile,
    uploadFolder,
    downloadRemote,
    downloadToMemory,
    cancelJob,
    clearFinished,
    setOverwriteStrategy,
    createFolder,
    renameRemote,
    deleteRemote,
    fmtSize,
  }
}

export type FileTransferApi = ReturnType<typeof useFileTransfer>
