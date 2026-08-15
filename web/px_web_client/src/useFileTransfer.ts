// 文件传输状态与操作(ft_data_channel):从 App.vue 抽出的 composable
// UI 在 FileTransferWindow.vue,调试钩子(window.__ft)仍在 App.vue 里组装
import { ref } from 'vue'
import { FileTransferClient } from './rtc/file_transfer'
import type { RemoteFileInfo, TransferTask } from './rtc/file_transfer'
import type { FsDirHandle, FsFileHandle } from './fs_access'
import { buildZip } from './zip'
import type { ZipEntry } from './zip'

export function useFileTransfer() {
  let ftClient: FileTransferClient | null = null
  let logFn: (msg: string) => void = () => {}

  const ftReady = ref(false)
  const ftPath = ref('/')
  const ftFiles = ref<RemoteFileInfo[]>([])
  const ftLoading = ref(false)
  const ftError = ref('')
  const ftTasks = ref<TransferTask[]>([])

  function log(msg: string) {
    logFn(msg)
  }

  // ft_data_channel onopen 时调用;成功后自动列出根目录(盘符列表)
  function initFt(dc: RTCDataChannel, deviceId: string, streamId: string, onLog: (msg: string) => void) {
    logFn = onLog
    ftClient = new FileTransferClient({
      dc,
      deviceId,
      streamId,
      onLog,
      onTasksChanged: (tasks) => {
        ftTasks.value = tasks
      },
    })
    ftReady.value = true
    log('文件传输通道已就绪')
    void refresh('/')
  }

  // 连接断开/清理时调用:失败所有进行中任务并复位状态
  function resetFt(reason: string) {
    ftClient?.failAll(reason)
    ftClient = null
    ftReady.value = false
    ftTasks.value = []
  }

  // App.vue 把 ft_data_channel 的 onmessage 转发到这里
  function handleChannelMessage(buf: ArrayBuffer) {
    ftClient?.handleChannelMessage(buf)
  }

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
    if (item.type === 2) return // 文件不进目录
    void refresh(item.path)
  }

  function up() {
    const p = ftPath.value.replace(/[\\/]+$/, '')
    if (!p || p === '/') return // 已在根(盘符列表)
    const idx = Math.max(p.lastIndexOf('/'), p.lastIndexOf('\\'))
    // "C:" 这一级再往上就是盘符列表
    void refresh(idx <= 2 ? '/' : p.slice(0, idx))
  }

  // 上传单个文件到远端当前目录
  async function uploadFile(file: File) {
    if (!ftClient) throw new Error('文件传输未就绪')
    try {
      await ftClient.upload(file, ftPath.value)
      log(`上传成功: ${file.name}`)
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log(`上传失败: ${msg}`)
      throw err instanceof Error ? err : new Error(msg)
    }
  }

  // 递归上传本地文件夹到远端目录(默认当前目录):
  // 先在远端建文件夹(render 自动命名)再重命名为本地文件夹名,随后逐层上传内容。
  // 重命名失败(远端已有同名文件夹)时退回使用自动生成的文件夹名。
  async function uploadFolder(dir: FsDirHandle, targetDir?: string): Promise<void> {
    if (!ftClient) throw new Error('文件传输未就绪')
    const parent = targetDir ?? ftPath.value
    const created = await ftClient.createFolder(parent)
    let folderPath = created
    try {
      folderPath = await ftClient.renameFile(created, dir.name)
    } catch {
      log(`远端已存在同名文件夹「${dir.name}」,内容将上传到: ${created}`)
    }
    for await (const handle of dir.values()) {
      if (handle.kind === 'file') {
        const file = await (handle as FsFileHandle).getFile()
        await ftClient.upload(file, folderPath)
        log(`上传成功: ${dir.name}/${file.name}`)
      } else {
        await uploadFolder(handle as FsDirHandle, folderPath)
      }
    }
  }

  // 下载远端文件到内存(供 File System Access 写盘/浏览器保存;失败记日志并返回 null)
  async function downloadRaw(
    item: RemoteFileInfo,
  ): Promise<{ taskId: string; name: string; data: Uint8Array; size: number } | null> {
    if (!ftClient) return null
    try {
      const { taskId, name, data, size } = await ftClient.download(item.path)
      log(`下载成功: ${name} (${size} bytes)`)
      return { taskId, name, data, size }
    } catch (err) {
      log(`下载失败: ${err instanceof Error ? err.message : String(err)}`)
      return null
    }
  }

  // 下载远端文件并触发浏览器保存(无 File System Access API 时的降级路径)
  async function downloadAndSave(item: RemoteFileInfo) {
    const r = await downloadRaw(item)
    if (r) {
      saveBlob(r.name, r.data)
      setTaskLocation(r.taskId, '浏览器下载目录')
    }
  }

  // 递归列出远端文件夹内容(render 返回扁平全路径列表:子孙文件夹 + 文件)
  async function listRemoteRecursive(path: string): Promise<RemoteFileInfo[]> {
    if (!ftClient) throw new Error('文件传输未就绪')
    const r = await ftClient.listDir(path, true)
    return r.files
  }

  // 远端全路径 -> 相对 folderPath 的路径(/ 分隔);不匹配时退回文件名
  function relToFolder(folderPath: string, fullPath: string): string {
    const base = folderPath.replace(/[\\/]+$/, '').replace(/\\/g, '/')
    const p = fullPath.replace(/\\/g, '/')
    if (p.toLowerCase().startsWith(base.toLowerCase() + '/')) return p.slice(base.length + 1)
    return p.split('/').pop() || fullPath
  }

  // 降级模式(无 File System Access)的文件夹下载:
  // 递归列出后逐文件下载到内存,打包成 store-only zip 走浏览器保存
  async function downloadFolderZip(item: RemoteFileInfo): Promise<{ ok: number; fail: number }> {
    if (!ftClient) throw new Error('文件传输未就绪')
    const entries = await listRemoteRecursive(item.path)
    const zipEntries: ZipEntry[] = [{ name: `${item.name}/`, data: new Uint8Array(0) }]
    // 先建目录条目(保留空文件夹)
    for (const e of entries) {
      if (e.type !== 2) {
        zipEntries.push({ name: `${item.name}/${relToFolder(item.path, e.path)}/`, data: new Uint8Array(0) })
      }
    }
    let ok = 0
    let fail = 0
    for (const e of entries) {
      if (e.type !== 2) continue
      const r = await downloadRaw(e)
      if (!r) {
        fail++
        continue
      }
      zipEntries.push({ name: `${item.name}/${relToFolder(item.path, e.path)}`, data: r.data })
      setTaskLocation(r.taskId, `浏览器下载目录(${item.name}.zip)`)
      ok++
    }
    if (ok === 0 && fail > 0) throw new Error('文件夹内文件全部下载失败')
    saveBlob(`${item.name}.zip`, buildZip(zipEntries))
    log(`文件夹已打包下载: ${item.name}.zip (${ok} 个文件${fail ? `, 失败 ${fail}` : ''})`)
    return { ok, fail }
  }

  // 回填任务落点(下载保存位置)
  function setTaskLocation(taskId: string, location: string) {
    ftClient?.setTaskLocation(taskId, location)
  }

  function saveBlob(name: string, data: Uint8Array) {
    const url = URL.createObjectURL(new Blob([data.slice().buffer]))
    const a = document.createElement('a')
    a.href = url
    a.download = name
    a.click()
    URL.revokeObjectURL(url)
  }

  function cancel(task: TransferTask) {
    ftClient?.cancel(task.taskId)
  }

  // 清除已完成/失败/取消的传输记录
  function clearFinished() {
    ftClient?.clearFinishedTasks()
  }

  // 在远端当前目录新建文件夹(名字由 render 自动生成),成功后刷新
  async function createFolder() {
    if (!ftClient) return
    await ftClient.createFolder(ftPath.value)
    await refresh()
  }

  async function renameRemote(item: RemoteFileInfo, newName: string) {
    if (!ftClient) return
    await ftClient.renameFile(item.path, newName)
    await refresh()
  }

  async function deleteRemote(item: RemoteFileInfo) {
    if (!ftClient) return
    await ftClient.deleteFile(item.path)
    await refresh()
  }

  function fmtSize(size: number): string {
    if (size < 1024) return `${size} B`
    if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KB`
    if (size < 1024 * 1024 * 1024) return `${(size / 1024 / 1024).toFixed(1)} MB`
    return `${(size / 1024 / 1024 / 1024).toFixed(2)} GB`
  }

  return {
    // 调试钩子(window.__ft)与 ftDc.onmessage 需要直接访问 client
    client: () => ftClient,
    ftReady,
    ftPath,
    ftFiles,
    ftLoading,
    ftError,
    ftTasks,
    initFt,
    resetFt,
    handleChannelMessage,
    refresh,
    enter,
    up,
    uploadFile,
    uploadFolder,
    downloadRaw,
    downloadAndSave,
    listRemoteRecursive,
    relToFolder,
    downloadFolderZip,
    setTaskLocation,
    cancel,
    clearFinished,
    createFolder,
    renameRemote,
    deleteRemote,
    fmtSize,
  }
}

export type FileTransferApi = ReturnType<typeof useFileTransfer>
