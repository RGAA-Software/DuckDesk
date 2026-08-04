// 独立文件传输窗口:对齐 C++ 端 src/gr_client/plugins/file_transfer_client/widget
// 左右双栏互传:本地(File System Access API 真实文件夹) <-> 远端(render 文件系统)
// 浏览器不支持 File System Access API 时降级:左栏为暂存区(选择文件/拖拽),下载走浏览器保存
// 下半部:统计条 + 传输记录表格
<script setup lang="ts">
import { computed, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import {
  IconX,
  IconFolder,
  IconFile,
  IconDeviceDesktop,
  IconUpload,
  IconDownload,
  IconArrowLeft,
  IconRefresh,
  IconFolderPlus,
  IconFolderOpen,
  IconEdit,
  IconTrash,
  IconCircleX,
} from '@tabler/icons-vue'
import type { FileTransferApi } from './useFileTransfer'
import type { RemoteFileInfo, TransferTask } from './rtc/file_transfer'
import { fsAccessSupported, pickDirectory, listDir, writeFile, ensureDir } from './fs_access'
import type { FsDirHandle, FsEntry, FsFileHandle } from './fs_access'

const props = defineProps<{
  deviceId: string
  ft: FileTransferApi
}>()

const visible = defineModel<boolean>('visible', { required: true })

const title = computed(() => `文件传输[web_${props.deviceId}]`)

// ---------- 本地栏:File System Access 文件夹浏览 ----------
const localRoot = ref<FsDirHandle | null>(null)
// 从根到当前目录的句柄链(含根)
const localStack = ref<FsDirHandle[]>([])
const localEntries = ref<FsEntry[]>([])
const localLoading = ref(false)
// 勾选的本地条目名(文件/文件夹均可,文件夹递归上传)
const selected = ref<Set<string>>(new Set())

const localPathText = computed(() => {
  if (!localStack.value.length) return ''
  return localStack.value.map((h) => h.name).join(' / ')
})
const selectedCount = computed(() => selected.value.size)

async function refreshLocal() {
  const cur = localStack.value[localStack.value.length - 1]
  if (!cur) return
  localLoading.value = true
  try {
    localEntries.value = await listDir(cur)
    // 目录内容变化后剔除已不存在的勾选
    const names = new Set(localEntries.value.map((e) => e.name))
    selected.value = new Set([...selected.value].filter((n) => names.has(n)))
  } catch (err) {
    ElMessage.error(`读取本地目录失败: ${err instanceof Error ? err.message : String(err)}`)
  } finally {
    localLoading.value = false
  }
}

async function pickLocalDir() {
  const dir = await pickDirectory()
  if (!dir) return
  localRoot.value = dir
  localStack.value = [dir]
  selected.value = new Set()
  await refreshLocal()
}

function localEnter(entry: FsEntry) {
  if (entry.kind !== 'directory') return
  localStack.value = [...localStack.value, entry.handle as FsDirHandle]
  void refreshLocal()
}

function localUp() {
  if (localStack.value.length <= 1) return
  localStack.value = localStack.value.slice(0, -1)
  void refreshLocal()
}

function toggleSelect(name: string, checked: boolean) {
  const s = new Set(selected.value)
  if (checked) s.add(name)
  else s.delete(name)
  selected.value = s
}

const uploading = ref(false)

async function uploadEntry(entry: FsEntry): Promise<boolean> {
  try {
    if (entry.kind === 'file') {
      const file = await (entry.handle as FsFileHandle).getFile()
      await props.ft.uploadFile(file)
    } else {
      // 文件夹:远端建同名目录后递归上传内容
      await props.ft.uploadFolder(entry.handle as FsDirHandle)
    }
    return true
  } catch (err) {
    ElMessage.error(`上传失败: ${entry.name} (${err instanceof Error ? err.message : String(err)})`)
    return false
  }
}

// 上传勾选的本地文件/文件夹到远端当前目录
async function uploadSelected() {
  if (uploading.value) return
  uploading.value = true
  try {
    for (const entry of localEntries.value) {
      if (!selected.value.has(entry.name)) continue
      if (await uploadEntry(entry)) {
        const s = new Set(selected.value)
        s.delete(entry.name)
        selected.value = s
      }
    }
    await props.ft.refresh()
  } finally {
    uploading.value = false
  }
}

// 远端文件 -> 本地栏当前目录(File System Access 直接写盘,不走浏览器下载)
async function downloadToLocal(item: RemoteFileInfo) {
  const cur = localStack.value[localStack.value.length - 1]
  if (!cur) {
    ElMessage.warning('请先在左栏选择本地目录')
    return
  }
  const r = await props.ft.downloadRaw(item)
  if (!r) {
    ElMessage.error(`下载失败: ${item.name}`)
    return
  }
  try {
    await writeFile(cur, r.name, r.data)
    const savedTo = `${localPathText.value} / ${r.name}`
    props.ft.setTaskLocation(r.taskId, savedTo)
    ElMessage.success(`已保存到本地: ${savedTo}`)
    await refreshLocal()
  } catch (err) {
    ElMessage.error(`写入本地文件失败: ${err instanceof Error ? err.message : String(err)}`)
  }
}

// 远端文件夹 -> 本地栏当前目录(递归列举 + 逐文件下载,还原目录结构)
const folderBusy = ref(false)

async function downloadFolderToLocal(item: RemoteFileInfo) {
  const cur = localStack.value[localStack.value.length - 1]
  if (!cur) {
    ElMessage.warning('请先在左栏选择本地目录')
    return
  }
  if (folderBusy.value) {
    ElMessage.warning('另一个文件夹正在下载中,请稍候')
    return
  }
  folderBusy.value = true
  try {
    const entries = await props.ft.listRemoteRecursive(item.path)
    const rootDir = await ensureDir(cur, [item.name])
    // 先建目录结构(保留空文件夹)
    for (const e of entries) {
      if (e.type !== 2) {
        await ensureDir(rootDir, props.ft.relToFolder(item.path, e.path).split('/'))
      }
    }
    let ok = 0
    let fail = 0
    for (const e of entries) {
      if (e.type !== 2) continue
      const r = await props.ft.downloadRaw(e)
      if (!r) {
        fail++
        continue
      }
      try {
        const segs = props.ft.relToFolder(item.path, e.path).split('/')
        const dir = await ensureDir(rootDir, segs.slice(0, -1))
        await writeFile(dir, segs[segs.length - 1], r.data)
        props.ft.setTaskLocation(r.taskId, `${localPathText.value} / ${item.name} / ${segs.join(' / ')}`)
        ok++
      } catch (err) {
        fail++
        ElMessage.error(`写入本地文件失败: ${e.name} (${err instanceof Error ? err.message : String(err)})`)
      }
    }
    if (fail === 0) {
      ElMessage.success(`文件夹下载完成: ${item.name} (${ok} 个文件)`)
    } else {
      ElMessage.warning(`文件夹下载结束: ${item.name} (成功 ${ok},失败 ${fail})`)
    }
    await refreshLocal()
  } catch (err) {
    ElMessage.error(`下载文件夹失败: ${err instanceof Error ? err.message : String(err)}`)
  } finally {
    folderBusy.value = false
  }
}

// 降级模式:文件夹打包成 zip 走浏览器保存
async function downloadFolderByZip(item: RemoteFileInfo) {
  if (folderBusy.value) {
    ElMessage.warning('另一个文件夹正在下载中,请稍候')
    return
  }
  folderBusy.value = true
  try {
    const { ok, fail } = await props.ft.downloadFolderZip(item)
    if (fail === 0) {
      ElMessage.success(`文件夹已打包下载: ${item.name}.zip (${ok} 个文件)`)
    } else {
      ElMessage.warning(`文件夹打包下载结束: 成功 ${ok},失败 ${fail}`)
    }
  } catch (err) {
    ElMessage.error(`下载文件夹失败: ${err instanceof Error ? err.message : String(err)}`)
  } finally {
    folderBusy.value = false
  }
}

// ---------- 降级模式:暂存区(不支持 File System Access API)----------
interface StagingItem {
  id: number
  file: File
}
const staging = ref<StagingItem[]>([])
let stagingSeq = 0
const fileInput = ref<HTMLInputElement | null>(null)
const dragOver = ref(false)

function pickFiles() {
  fileInput.value?.click()
}

function addFiles(files: Iterable<File>) {
  for (const f of files) {
    // 同名同大小视为重复,跳过
    if (staging.value.some((s) => s.file.name === f.name && s.file.size === f.size)) continue
    staging.value.push({ id: ++stagingSeq, file: f })
  }
}

function onFilesChosen(ev: Event) {
  const inputEl = ev.target as HTMLInputElement
  if (inputEl.files) addFiles(inputEl.files)
  inputEl.value = ''
}

function onDrop(ev: DragEvent) {
  dragOver.value = false
  if (ev.dataTransfer?.files) addFiles(ev.dataTransfer.files)
}

function removeStaging(id: number) {
  staging.value = staging.value.filter((s) => s.id !== id)
}

// 依次上传到远端当前目录:成功的移出暂存区,失败的保留并提示
async function uploadAll() {
  if (uploading.value) return
  uploading.value = true
  try {
    for (const item of [...staging.value]) {
      try {
        await props.ft.uploadFile(item.file)
        removeStaging(item.id)
      } catch (err) {
        ElMessage.error(`上传失败: ${item.file.name} (${err instanceof Error ? err.message : String(err)})`)
      }
    }
    await props.ft.refresh()
  } finally {
    uploading.value = false
  }
}

// ---------- 远端栏 ----------
function fmtDate(date: number): string {
  if (!date) return '-'
  return new Date(date * 1000).toLocaleString()
}

// 双击:文件夹/盘符进目录,文件直接下载
function onRowDblClick(row: RemoteFileInfo) {
  if (row.type === 2) {
    void onDownload(row)
  } else {
    props.ft.enter(row)
  }
}

// 下载:支持 File System Access 时写入本地栏当前目录,否则浏览器保存
// 文件夹:FS Access 模式递归还原目录结构;降级模式打包成 zip 保存
function onDownload(item: RemoteFileInfo) {
  if (item.type !== 2) {
    return fsAccessSupported ? downloadFolderToLocal(item) : downloadFolderByZip(item)
  }
  if (fsAccessSupported) {
    return downloadToLocal(item)
  }
  return props.ft.downloadAndSave(item)
}

async function onCreateFolder() {
  try {
    await props.ft.createFolder()
    ElMessage.success('新建文件夹成功')
  } catch (err) {
    ElMessage.error(err instanceof Error ? err.message : String(err))
  }
}

async function onRename(item: RemoteFileInfo) {
  let newName = ''
  try {
    const r = await ElMessageBox.prompt('输入新名称(仅文件名,不含路径)', `重命名: ${item.name}`, {
      inputValue: item.name,
      inputValidator: (v) => (!!v && !/[/\\]/.test(v)) || '名称不能为空且不能包含 / \\',
      confirmButtonText: '重命名',
      cancelButtonText: '取消',
    })
    newName = r.value
  } catch {
    return
  }
  try {
    await props.ft.renameRemote(item, newName)
    ElMessage.success('重命名成功')
  } catch (err) {
    ElMessage.error(err instanceof Error ? err.message : String(err))
  }
}

async function onDelete(item: RemoteFileInfo) {
  try {
    await ElMessageBox.confirm(`确认删除「${item.name}」?该操作不可恢复。`, '删除', {
      type: 'warning',
      confirmButtonText: '删除',
      cancelButtonText: '取消',
    })
  } catch {
    return
  }
  try {
    await props.ft.deleteRemote(item)
    ElMessage.success('删除成功')
  } catch (err) {
    ElMessage.error(err instanceof Error ? err.message : String(err))
  }
}

// ---------- 传输记录 / 统计 ----------
const tasks = computed(() => props.ft.ftTasks.value)
const statTotal = computed(() => tasks.value.length)
const statDone = computed(() => tasks.value.filter((t) => t.state === 'done').length)
const statFailed = computed(() => tasks.value.filter((t) => t.state === 'error' || t.state === 'cancelled').length)
const upSpeed = computed(() =>
  tasks.value.filter((t) => t.state === 'running' && t.direction === 'upload').reduce((s, t) => s + t.speedBps, 0),
)
const downSpeed = computed(() =>
  tasks.value.filter((t) => t.state === 'running' && t.direction === 'download').reduce((s, t) => s + t.speedBps, 0),
)

function taskPercent(t: TransferTask): number {
  return t.total > 0 ? Math.min(100, Math.floor((t.transferred / t.total) * 100)) : 0
}

function taskStateText(t: TransferTask): string {
  switch (t.state) {
    case 'running':
      return '传输中'
    case 'done':
      return '完成'
    case 'cancelled':
      return '已取消'
    default:
      return '失败'
  }
}

// 速度统一用 MB/s 小数表示(低于 1MB/s 也是 0.xx MB/s,不用 KB)
function fmtSpeed(bps: number): string {
  if (bps <= 0) return '-'
  return `${(bps / 1024 / 1024).toFixed(2)} MB/s`
}
</script>

<template>
  <div v-if="visible" class="ftw-mask">
    <div class="ftw-window">
      <!-- 标题栏 -->
      <div class="ftw-titlebar">
        <span class="ftw-title">{{ title }}</span>
        <button class="ftw-close" title="关闭" @click="visible = false">
          <IconX :size="18" />
        </button>
      </div>

      <!-- 上半部:本地 / 远端 双栏互传 -->
      <div class="ftw-main">
        <!-- 左栏:本地 -->
        <div class="ftw-pane">
          <template v-if="fsAccessSupported">
            <div class="ftw-pane-header">
              <span class="ftw-pane-title">本地</span>
              <el-button size="small" @click="pickLocalDir">
                <el-icon><IconFolderOpen /></el-icon>&nbsp;选择目录
              </el-button>
              <el-button size="small" :disabled="localStack.length <= 1" @click="localUp">
                <el-icon><IconArrowLeft /></el-icon>&nbsp;上级
              </el-button>
              <el-button size="small" :disabled="!localRoot || localLoading" @click="refreshLocal">
                <el-icon><IconRefresh /></el-icon>&nbsp;刷新
              </el-button>
              <el-button
                size="small"
                type="primary"
                :disabled="!selectedCount || !ft.ftReady.value"
                :loading="uploading"
                @click="uploadSelected"
              >
                <el-icon><IconUpload /></el-icon>&nbsp;上传选中({{ selectedCount }})
              </el-button>
            </div>
            <div v-if="localRoot" class="ftw-local-path" :title="localPathText">
              {{ localPathText }}
            </div>
            <div v-if="!localRoot" class="ftw-local-empty">
              点「选择目录」挑选一个本地文件夹,即可与远端互传文件
            </div>
            <div v-else v-loading="localLoading" class="ftw-local-list">
              <div
                v-for="e in localEntries"
                :key="e.name"
                class="ftw-local-item"
                @dblclick="localEnter(e)"
              >
                <el-checkbox
                  :model-value="selected.has(e.name)"
                  class="ftw-local-check"
                  @change="(v: string | number | boolean) => toggleSelect(e.name, !!v)"
                  @dblclick.stop
                />
                <el-icon class="ftw-entry-icon">
                  <IconFolder v-if="e.kind === 'directory'" />
                  <IconFile v-else />
                </el-icon>
                <span class="ftw-staging-name" :title="e.name">{{ e.name }}</span>
                <span class="ftw-staging-size">{{ e.kind === 'file' ? ft.fmtSize(e.size) : '-' }}</span>
                <el-button
                  size="small"
                  link
                  type="primary"
                  :disabled="!ft.ftReady.value"
                  :title="e.kind === 'file' ? '上传到远端当前目录' : '递归上传文件夹到远端当前目录'"
                  @click="uploadEntry(e)"
                >
                  <el-icon><IconUpload /></el-icon>
                </el-button>
              </div>
            </div>
          </template>

          <!-- 降级:暂存区 -->
          <template v-else>
            <div class="ftw-pane-header">
              <span class="ftw-pane-title">本地(待传文件)</span>
              <el-button size="small" @click="pickFiles">选择文件</el-button>
              <el-button
                size="small"
                type="primary"
                :disabled="!staging.length || !ft.ftReady.value"
                :loading="uploading"
                @click="uploadAll"
              >
                全部上传
              </el-button>
              <input ref="fileInput" type="file" multiple class="ftw-file-input" @change="onFilesChosen" />
            </div>
            <div
              class="ftw-drop"
              :class="{ over: dragOver }"
              @dragover.prevent="dragOver = true"
              @dragleave.prevent="dragOver = false"
              @drop.prevent="onDrop"
            >
              <div v-if="!staging.length" class="ftw-drop-hint">拖拽文件到此处,或点「选择文件」</div>
              <div v-for="s in staging" :key="s.id" class="ftw-staging-item">
                <el-icon class="ftw-entry-icon"><IconFile /></el-icon>
                <span class="ftw-staging-name" :title="s.file.name">{{ s.file.name }}</span>
                <span class="ftw-staging-size">{{ ft.fmtSize(s.file.size) }}</span>
                <el-button size="small" link type="danger" @click="removeStaging(s.id)">
                  <el-icon><IconCircleX /></el-icon>
                </el-button>
              </div>
            </div>
          </template>
        </div>

        <div class="ftw-divider"></div>

        <!-- 右栏:远端 -->
        <div class="ftw-pane">
          <div class="ftw-pane-header">
            <span class="ftw-pane-title">远端</span>
            <el-button size="small" :disabled="!ft.ftReady.value || ft.ftLoading.value" @click="ft.up()">
              <el-icon><IconArrowLeft /></el-icon>&nbsp;上级
            </el-button>
            <el-input
              v-model="ft.ftPath.value"
              size="small"
              class="ftw-path-input"
              :disabled="!ft.ftReady.value"
              @keyup.enter="ft.refresh()"
            />
            <el-button
              size="small"
              :loading="ft.ftLoading.value"
              :disabled="!ft.ftReady.value"
              @click="ft.refresh()"
            >
              <el-icon><IconRefresh /></el-icon>&nbsp;刷新
            </el-button>
            <el-button size="small" :disabled="!ft.ftReady.value" @click="onCreateFolder">
              <el-icon><IconFolderPlus /></el-icon>&nbsp;新建文件夹
            </el-button>
          </div>
          <el-alert
            v-if="ft.ftError.value"
            :title="ft.ftError.value"
            type="error"
            :closable="false"
            class="ftw-error"
          />
          <div class="ftw-table-wrap">
            <el-table
              v-loading="ft.ftLoading.value"
              :data="ft.ftFiles.value"
              size="small"
              height="100%"
              @row-dblclick="onRowDblClick"
            >
              <el-table-column label="名称" min-width="200">
                <template #default="{ row }">
                  <span class="ftw-file-name" :title="row.path">
                    <el-icon class="ftw-entry-icon">
                      <IconDeviceDesktop v-if="row.type === 0" />
                      <IconFile v-else-if="row.type === 2" />
                      <IconFolder v-else />
                    </el-icon>
                    {{ row.name }}
                  </span>
                </template>
              </el-table-column>
              <el-table-column label="大小" width="100">
                <template #default="{ row }">
                  {{ row.type === 2 ? ft.fmtSize(row.size) : '-' }}
                </template>
              </el-table-column>
              <el-table-column label="修改日期" width="150">
                <template #default="{ row }">
                  {{ fmtDate(row.date) }}
                </template>
              </el-table-column>
              <el-table-column label="操作" width="150" fixed="right">
                <template #default="{ row }">
                  <el-button size="small" link type="primary" title="下载到本地" @click="onDownload(row)">
                    <el-icon><IconDownload /></el-icon>
                  </el-button>
                  <el-button size="small" link type="primary" title="重命名" @click="onRename(row)">
                    <el-icon><IconEdit /></el-icon>
                  </el-button>
                  <el-button size="small" link type="danger" title="删除" @click="onDelete(row)">
                    <el-icon><IconTrash /></el-icon>
                  </el-button>
                </template>
              </el-table-column>
            </el-table>
          </div>
        </div>
      </div>

      <!-- 下半部:统计条 + 传输记录 -->
      <div class="ftw-bottom">
        <div class="ftw-stats">
          <span>任务: {{ statTotal }} / 完成 {{ statDone }} / 失败 {{ statFailed }}</span>
          <span class="ftw-speed up">
            <el-icon><IconUpload /></el-icon> {{ fmtSpeed(upSpeed) }}
          </span>
          <span class="ftw-speed down">
            <el-icon><IconDownload /></el-icon> {{ fmtSpeed(downSpeed) }}
          </span>
          <el-button size="small" class="ftw-clear" :disabled="!statDone && !statFailed" @click="ft.clearFinished()">
            清除已完成
          </el-button>
        </div>
        <div class="ftw-table-wrap">
          <el-table :data="tasks" size="small" height="100%">
          <el-table-column label="方向" width="80">
            <template #default="{ row }">
              <span class="ftw-direction">
                <el-icon>
                  <IconUpload v-if="row.direction === 'upload'" />
                  <IconDownload v-else />
                </el-icon>
                {{ row.direction === 'upload' ? '上传' : '下载' }}
              </span>
            </template>
          </el-table-column>
          <el-table-column label="文件名" min-width="180">
            <template #default="{ row }">
              <span class="ftw-file-name" :title="row.fileName">{{ row.fileName }}</span>
            </template>
          </el-table-column>
          <el-table-column label="大小" width="90">
            <template #default="{ row }">
              {{ row.total > 0 ? ft.fmtSize(row.total) : '-' }}
            </template>
          </el-table-column>
          <el-table-column label="位置" min-width="160">
            <template #default="{ row }">
              <span class="ftw-location" :title="row.location ?? ''">{{ row.location || '-' }}</span>
            </template>
          </el-table-column>
          <el-table-column label="进度" min-width="140">
            <template #default="{ row }">
              <el-progress
                :percentage="taskPercent(row)"
                :status="row.state === 'done' ? 'success' : row.state === 'error' || row.state === 'cancelled' ? 'exception' : undefined"
              />
            </template>
          </el-table-column>
          <el-table-column label="速度" width="100">
            <template #default="{ row }">
              {{ row.state === 'running' ? fmtSpeed(row.speedBps) : '-' }}
            </template>
          </el-table-column>
          <el-table-column label="状态" width="80">
            <template #default="{ row }">
              <span :class="{ 'ftw-state-error': row.state === 'error' }" :title="row.error ?? ''">
                {{ taskStateText(row) }}
              </span>
            </template>
          </el-table-column>
          <el-table-column label="操作" width="70">
            <template #default="{ row }">
              <el-button
                v-if="row.state === 'running'"
                size="small"
                link
                type="danger"
                @click="ft.cancel(row)"
              >
                取消
              </el-button>
            </template>
          </el-table-column>
          </el-table>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.ftw-mask {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.55);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 100;
}
.ftw-window {
  width: min(1366px, 95vw);
  height: min(768px, 92vh);
  background: #fff;
  border-radius: 8px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  box-shadow: 0 8px 40px rgba(0, 0, 0, 0.4);
}
.ftw-titlebar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 12px;
  height: 40px;
  background: #2b2f36;
  color: #fff;
  flex-shrink: 0;
}
.ftw-title {
  font-size: 14px;
  font-weight: 600;
}
.ftw-close {
  border: none;
  background: transparent;
  color: #ccc;
  width: 32px;
  height: 32px;
  cursor: pointer;
  border-radius: 4px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.ftw-close:hover {
  background: #e81123;
  color: #fff;
}
.ftw-main {
  flex: 1;
  display: flex;
  min-height: 0;
}
.ftw-pane {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
  min-height: 0;
  padding: 8px;
  gap: 6px;
}
.ftw-divider {
  width: 1px;
  background: #e4e7ed;
  flex-shrink: 0;
}
.ftw-pane-header {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-shrink: 0;
  flex-wrap: wrap;
}
.ftw-pane-title {
  font-size: 13px;
  font-weight: 600;
  color: #303133;
  margin-right: auto;
}
.ftw-file-input {
  display: none;
}
.ftw-local-path {
  flex-shrink: 0;
  font-size: 12px;
  color: #606266;
  background: #f5f7fa;
  border-radius: 4px;
  padding: 4px 8px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.ftw-local-empty {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #909399;
  font-size: 13px;
  border: 1px dashed #c0c4cc;
  border-radius: 6px;
  padding: 0 24px;
  text-align: center;
}
.ftw-local-list {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  border: 1px solid #e4e7ed;
  border-radius: 6px;
  padding: 4px;
}
.ftw-local-item {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 3px 6px;
  border-radius: 4px;
  font-size: 13px;
}
.ftw-local-item:hover {
  background: #f5f5f5;
}
.ftw-local-check {
  height: auto;
  margin-right: 0;
}
.ftw-entry-icon {
  color: #606266;
  vertical-align: middle;
}
.ftw-drop {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  border: 1px dashed #c0c4cc;
  border-radius: 6px;
  padding: 6px;
}
.ftw-drop.over {
  border-color: #409eff;
  background: #ecf5ff;
}
.ftw-drop-hint {
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #909399;
  font-size: 13px;
}
.ftw-staging-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 6px;
  border-radius: 4px;
}
.ftw-staging-item:hover {
  background: #f5f5f5;
}
.ftw-staging-name {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 13px;
}
.ftw-staging-size {
  color: #909399;
  font-size: 12px;
  flex-shrink: 0;
}
.ftw-path-input {
  flex: 1;
  min-width: 120px;
}
.ftw-error {
  flex-shrink: 0;
}
.ftw-table-wrap {
  flex: 1;
  min-height: 0;
}
.ftw-file-name {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  display: inline-flex;
  align-items: center;
  gap: 4px;
}
.ftw-direction {
  display: inline-flex;
  align-items: center;
  gap: 4px;
}
.ftw-bottom {
  height: 220px;
  flex-shrink: 0;
  border-top: 1px solid #e4e7ed;
  display: flex;
  flex-direction: column;
  padding: 6px 8px;
  gap: 4px;
}
.ftw-stats {
  display: flex;
  align-items: center;
  gap: 16px;
  font-size: 13px;
  color: #303133;
  flex-shrink: 0;
}
.ftw-speed {
  font-family: monospace;
  display: inline-flex;
  align-items: center;
  gap: 4px;
}
.ftw-speed.up {
  color: #e6a23c;
}
.ftw-speed.down {
  color: #409eff;
}
.ftw-clear {
  margin-left: auto;
}
.ftw-state-error {
  color: #f56c6c;
}
.ftw-location {
  display: inline-block;
  max-width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  color: #606266;
  font-size: 12px;
  vertical-align: middle;
}
</style>
