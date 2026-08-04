// 文件传输状态与操作(ft_data_channel):从 App.vue 抽出的 composable
// UI 在 FileTransferWindow.vue,调试钩子(window.__ft)仍在 App.vue 里组装
import { ref } from 'vue'
import { FileTransferClient } from './rtc/file_transfer'
import type { RemoteFileInfo, TransferTask } from './rtc/file_transfer'

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

  // 下载远端文件并触发浏览器保存
  async function downloadAndSave(item: RemoteFileInfo) {
    if (!ftClient) return
    try {
      const { name, data, size } = await ftClient.download(item.path)
      saveBlob(name, data)
      log(`下载成功: ${name} (${size} bytes)`)
    } catch (err) {
      log(`下载失败: ${err instanceof Error ? err.message : String(err)}`)
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
    downloadAndSave,
    cancel,
    clearFinished,
    createFolder,
    renameRemote,
    deleteRemote,
    fmtSize,
  }
}

export type FileTransferApi = ReturnType<typeof useFileTransfer>
