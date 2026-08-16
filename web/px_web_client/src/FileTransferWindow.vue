// 文件传输窗口(rustdesk 协议,阶段 4 全新实现)
// 三栏布局:本地(File System Access 浏览/暂存区降级) | 远端(render 文件系统) | 传输队列
// 交互对齐 Qt 版:目录导航、新建文件夹、重命名、删除(目录递归确认)、上传/下载、
// 拖拽(OS 拖入 + 面板互拖)、覆盖确认弹框(跳过/覆盖/续传 + 应用到全部)、进度/速度/取消
<script setup lang="ts">
import { computed, onBeforeUnmount, ref } from 'vue'
import { useI18n } from 'vue-i18n'
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
import { isRemoteDir, isRemoteFile } from './useFileTransfer'
import { FT_TYPE_DRIVE, joinRemote } from './rtc/file_transfer'
import type { FtJob, OverwriteDecision, OverwriteRequest, RemoteFileInfo } from './rtc/file_transfer'
import { fsAccessSupported, pickDirectory, listDir } from './fs_access'
import type { FsDirHandle, FsEntry, FsFileHandle } from './fs_access'

const props = defineProps<{
  deviceId: string
  ft: FileTransferApi
}>()

const { t } = useI18n()
const visible = defineModel<boolean>('visible', { required: true })

const title = computed(() => t('ft.title', { id: props.deviceId }))

// ---------- 本地栏:File System Access 文件夹浏览 ----------
const localRoot = ref<FsDirHandle | null>(null)
const localStack = ref<FsDirHandle[]>([])
const localEntries = ref<FsEntry[]>([])
const localLoading = ref(false)
const selected = ref<Set<string>>(new Set())

const localPathText = computed(() => localStack.value.map((h) => h.name).join(' / '))
const selectedCount = computed(() => selected.value.size)
const currentLocalDir = computed(() => localStack.value[localStack.value.length - 1] ?? null)

async function refreshLocal() {
  const cur = currentLocalDir.value
  if (!cur) return
  localLoading.value = true
  try {
    localEntries.value = await listDir(cur)
    const names = new Set(localEntries.value.map((e) => e.name))
    selected.value = new Set([...selected.value].filter((n) => names.has(n)))
  } catch (err) {
    ElMessage.error(t('ft.readLocalFail', { err: err instanceof Error ? err.message : String(err) }))
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

// ---------- 上传 ----------
async function uploadEntry(entry: FsEntry): Promise<boolean> {
  try {
    if (entry.kind === 'file') {
      const file = await (entry.handle as FsFileHandle).getFile()
      props.ft.uploadFile(file)
    } else {
      await props.ft.uploadFolder(entry.handle as FsDirHandle)
    }
    return true
  } catch (err) {
    ElMessage.error(t('ft.uploadFail', { name: entry.name, err: err instanceof Error ? err.message : String(err) }))
    return false
  }
}

async function uploadSelected() {
  for (const entry of localEntries.value) {
    if (!selected.value.has(entry.name)) continue
    if (await uploadEntry(entry)) {
      const s = new Set(selected.value)
      s.delete(entry.name)
      selected.value = s
    }
  }
}

// OS 拖入(含文件夹):webkitGetAsEntry 递归展开为上传作业
// (lib.dom 的 FileSystemEntry 声明不全,这里用最小结构契约)
interface OsEntry {
  isFile: boolean
  isDirectory: boolean
  name: string
  file(cb: (f: File) => void, err?: (e: unknown) => void): void
  createReader(): OsEntryReader
}

interface OsEntryReader {
  readEntries(cb: (es: OsEntry[]) => void, err?: (e: unknown) => void): void
}

async function readAllEntries(reader: OsEntryReader): Promise<OsEntry[]> {
  // readEntries 每次最多 100 项,需循环读到空
  const out: OsEntry[] = []
  for (;;) {
    const batch = await new Promise<OsEntry[]>((resolve, reject) => reader.readEntries(resolve, reject))
    if (!batch.length) return out
    out.push(...batch)
  }
}

async function traverseOsEntry(
  entry: OsEntry,
  prefix: string,
  items: Array<{ name: string; file: File; size: number; modifiedTime: number }>,
  emptyDirs: string[],
): Promise<void> {
  const rel = prefix ? `${prefix}/${entry.name}` : entry.name
  if (entry.isFile) {
    const file = await new Promise<File>((resolve, reject) => entry.file(resolve, reject))
    items.push({ name: rel, file, size: file.size, modifiedTime: Math.floor(file.lastModified / 1000) })
  } else if (entry.isDirectory) {
    const children = await readAllEntries(entry.createReader())
    if (!children.length) emptyDirs.push(rel)
    for (const c of children) await traverseOsEntry(c, rel, items, emptyDirs)
  }
}

// 把 OS 拖入的 items 展开并上传(文件/文件夹混投)
async function uploadOsItems(dt: DataTransfer) {
  const entries: OsEntry[] = []
  for (const item of Array.from(dt.items)) {
    const e = item.webkitGetAsEntry?.() as unknown as OsEntry | null
    if (e) entries.push(e)
  }
  if (!entries.length) {
    for (const f of Array.from(dt.files)) props.ft.uploadFile(f)
    return
  }
  for (const e of entries) {
    try {
      if (e.isFile) {
        const file = await new Promise<File>((resolve, reject) => e.file(resolve, reject))
        props.ft.uploadFile(file)
      } else {
        const items: Array<{ name: string; file: File; size: number; modifiedTime: number }> = []
        const emptyDirs: string[] = []
        const children = await readAllEntries(e.createReader())
        if (!children.length) emptyDirs.push('')
        for (const c of children) await traverseOsEntry(c, '', items, emptyDirs)
        const client = props.ft.client()
        if (!client) throw new Error('ft not ready')
        const remoteTo = joinRemote(props.ft.ftPath.value, e.name)
        for (const rel of emptyDirs) {
          await client.createDir(rel ? joinRemote(remoteTo, rel) : remoteTo)
        }
        client.upload(items, remoteTo, e.name)
      }
    } catch (err) {
      ElMessage.error(t('ft.uploadFail', { name: e.name, err: err instanceof Error ? err.message : String(err) }))
    }
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
const localDragOver = ref(false)

function pickFiles() {
  fileInput.value?.click()
}

function addFiles(files: Iterable<File>) {
  for (const f of files) {
    if (staging.value.some((s) => s.file.name === f.name && s.file.size === f.size)) continue
    staging.value.push({ id: ++stagingSeq, file: f })
  }
}

function onFilesChosen(ev: Event) {
  const inputEl = ev.target as HTMLInputElement
  if (inputEl.files) addFiles(inputEl.files)
  inputEl.value = ''
}

function removeStaging(id: number) {
  staging.value = staging.value.filter((s) => s.id !== id)
}

function uploadAll() {
  for (const item of [...staging.value]) {
    try {
      props.ft.uploadFile(item.file)
      removeStaging(item.id)
    } catch (err) {
      ElMessage.error(t('ft.uploadFail', { name: item.file.name, err: err instanceof Error ? err.message : String(err) }))
    }
  }
}

// ---------- 下载 ----------
function downloadSink(): { type: 'fs' | 'browser'; root?: FsDirHandle } {
  if (fsAccessSupported && currentLocalDir.value) {
    return { type: 'fs', root: currentLocalDir.value }
  }
  return { type: 'browser' }
}

function onDownload(item: RemoteFileInfo) {
  try {
    props.ft.downloadRemote(item, downloadSink())
  } catch (err) {
    ElMessage.error(t('ft.downloadFail', { name: item.name, err: err instanceof Error ? err.message : String(err) }))
  }
}

// ---------- 远端栏操作 ----------
function fmtDate(sec: number): string {
  if (!sec) return '-'
  return new Date(sec * 1000).toLocaleString()
}

function onRowDblClick(row: RemoteFileInfo) {
  if (isRemoteFile(row.type)) {
    onDownload(row)
  } else {
    props.ft.enter(row)
  }
}

async function onCreateFolder() {
  let name = ''
  try {
    const r = await ElMessageBox.prompt(t('ft.newFolderPrompt'), t('ft.createFolder'), {
      inputValue: t('ft.newFolderDefault'),
      inputValidator: (v) => (!!v && !/[/\\]/.test(v)) || t('ft.renameInvalid'),
      confirmButtonText: t('common.ok'),
      cancelButtonText: t('common.cancel'),
    })
    name = r.value
  } catch {
    return
  }
  try {
    await props.ft.createFolder(name)
    ElMessage.success(t('ft.mkdirOk'))
  } catch (err) {
    ElMessage.error(err instanceof Error ? err.message : String(err))
  }
}

async function onRename(item: RemoteFileInfo) {
  let newName = ''
  try {
    const r = await ElMessageBox.prompt(t('ft.renamePrompt'), t('ft.renameTitle', { name: item.name }), {
      inputValue: item.name,
      inputValidator: (v) => (!!v && !/[/\\]/.test(v)) || t('ft.renameInvalid'),
      confirmButtonText: t('ft.rename'),
      cancelButtonText: t('common.cancel'),
    })
    newName = r.value
  } catch {
    return
  }
  try {
    await props.ft.renameRemote(item, newName)
    ElMessage.success(t('ft.renameOk'))
  } catch (err) {
    ElMessage.error(err instanceof Error ? err.message : String(err))
  }
}

async function onDelete(item: RemoteFileInfo) {
  const isDir = isRemoteDir(item.type)
  try {
    await ElMessageBox.confirm(
      isDir ? t('ft.deleteDirConfirm', { name: item.name }) : t('ft.deleteConfirm', { name: item.name }),
      t('ft.deleteTitle'),
      { type: 'warning', confirmButtonText: t('ft.delete'), cancelButtonText: t('common.cancel') },
    )
  } catch {
    return
  }
  try {
    await props.ft.deleteRemote(item)
    ElMessage.success(t('ft.deleteOk'))
  } catch (err) {
    ElMessage.error(err instanceof Error ? err.message : String(err))
  }
}

// ---------- 拖拽:面板互拖 + OS 拖入 ----------
// HTML5 DnD 的 dataTransfer 只能带字符串,条目对象放这里
let dragPayload:
  | { kind: 'local'; entries: FsEntry[] }
  | { kind: 'remote'; item: RemoteFileInfo }
  | null = null

const remoteDragOver = ref(false)

function onLocalDragStart(entry: FsEntry) {
  dragPayload = { kind: 'local', entries: [entry] }
}

function onLocalDragEnd() {
  dragPayload = null
}

function onRemoteDragStart(item: RemoteFileInfo) {
  dragPayload = { kind: 'remote', item }
}

function onRemoteDragEnd() {
  dragPayload = null
}

function onRemoteDrop(ev: DragEvent) {
  remoteDragOver.value = false
  if (dragPayload?.kind === 'local') {
    // 本地栏拖到远端:上传
    for (const entry of dragPayload.entries) void uploadEntry(entry)
  } else if (ev.dataTransfer?.items?.length || ev.dataTransfer?.files?.length) {
    // OS 拖入:直接上传到远端当前目录
    void uploadOsItems(ev.dataTransfer)
  }
  dragPayload = null
}

function onLocalDrop(ev: DragEvent) {
  localDragOver.value = false
  if (dragPayload?.kind === 'remote') {
    // 远端栏拖到本地:下载到本地当前目录(FS Access 直写;否则浏览器保存)
    onDownload(dragPayload.item)
  } else if (!fsAccessSupported && ev.dataTransfer?.files?.length) {
    addFiles(ev.dataTransfer.files)
  }
  dragPayload = null
}

// ---------- 覆盖确认弹框 ----------
const overwriteVisible = ref(false)
const overwriteReq = ref<OverwriteRequest | null>(null)
const overwriteApplyAll = ref(false)
let overwriteResolve: ((d: OverwriteDecision) => void) | null = null

props.ft.setOverwriteHandler(
  (req) =>
    new Promise<OverwriteDecision>((resolve) => {
      overwriteReq.value = req
      overwriteApplyAll.value = false
      overwriteVisible.value = true
      overwriteResolve = resolve
    }),
)
onBeforeUnmount(() => props.ft.setOverwriteHandler(null))

function decideOverwrite(d: OverwriteDecision) {
  overwriteVisible.value = false
  if (overwriteApplyAll.value && overwriteReq.value) {
    props.ft.setOverwriteStrategy(overwriteReq.value.isUpload ? 'upload' : 'download', d)
  }
  overwriteResolve?.(d)
  overwriteResolve = null
  overwriteReq.value = null
}

// ---------- 传输队列 ----------
const jobs = computed(() => props.ft.ftJobs.value)
const statTotal = computed(() => jobs.value.length)
const statDone = computed(() => jobs.value.filter((j) => j.state === 'done').length)
const statFailed = computed(() => jobs.value.filter((j) => j.state === 'error' || j.state === 'cancelled').length)
const upSpeed = computed(() =>
  jobs.value.filter((j) => j.state === 'running' && j.direction === 'upload').reduce((s, j) => s + j.speedBps, 0),
)
const downSpeed = computed(() =>
  jobs.value.filter((j) => j.state === 'running' && j.direction === 'download').reduce((s, j) => s + j.speedBps, 0),
)

function jobPercent(j: FtJob): number {
  return j.totalSize > 0 ? Math.min(100, Math.floor((j.finishedSize / j.totalSize) * 100)) : j.state === 'done' ? 100 : 0
}

function jobStateText(j: FtJob): string {
  switch (j.state) {
    case 'pending':
      return t('ft.statePending')
    case 'running':
      return t('ft.stateRunning')
    case 'done':
      return t('ft.stateDone')
    case 'cancelled':
      return t('ft.stateCancelled')
    default:
      return t('ft.stateError')
  }
}

// 速度统一用 MB/s 小数表示(低于 1MB/s 也是 0.xx MB/s)
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
        <button class="ftw-close" :title="t('common.close')" @click="visible = false">
          <IconX :size="18" />
        </button>
      </div>

      <!-- 上半部:本地 / 远端 双栏互传 -->
      <div class="ftw-main">
        <!-- 左栏:本地 -->
        <div
          class="ftw-pane"
          :class="{ 'drag-over': localDragOver }"
          @dragover.prevent="localDragOver = true"
          @dragleave.prevent="localDragOver = false"
          @drop.prevent="onLocalDrop"
        >
          <template v-if="fsAccessSupported">
            <div class="ftw-pane-header">
              <span class="ftw-pane-title">{{ t('ft.local') }}</span>
              <el-button size="small" @click="pickLocalDir">
                <el-icon><IconFolderOpen /></el-icon>&nbsp;{{ t('ft.pickDir') }}
              </el-button>
              <el-button size="small" :disabled="localStack.length <= 1" @click="localUp">
                <el-icon><IconArrowLeft /></el-icon>&nbsp;{{ t('ft.up') }}
              </el-button>
              <el-button size="small" :disabled="!localRoot || localLoading" @click="refreshLocal">
                <el-icon><IconRefresh /></el-icon>&nbsp;{{ t('ft.refresh') }}
              </el-button>
              <el-button
                size="small"
                type="primary"
                :disabled="!selectedCount || !ft.ftReady.value"
                @click="uploadSelected"
              >
                <el-icon><IconUpload /></el-icon>&nbsp;{{ t('ft.uploadSelected', { n: selectedCount }) }}
              </el-button>
            </div>
            <div v-if="localRoot" class="ftw-local-path" :title="localPathText">{{ localPathText }}</div>
            <div v-if="!localRoot" class="ftw-local-empty">{{ t('ft.localEmpty') }}</div>
            <div v-else v-loading="localLoading" class="ftw-local-list">
              <div
                v-for="e in localEntries"
                :key="e.name"
                class="ftw-local-item"
                draggable="true"
                @dragstart="onLocalDragStart(e)"
                @dragend="onLocalDragEnd"
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
                  :title="e.kind === 'file' ? t('ft.uploadFileTip') : t('ft.uploadFolderTip')"
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
              <span class="ftw-pane-title">{{ t('ft.localStaging') }}</span>
              <el-button size="small" @click="pickFiles">{{ t('ft.pickFiles') }}</el-button>
              <el-button size="small" type="primary" :disabled="!staging.length || !ft.ftReady.value" @click="uploadAll">
                {{ t('ft.uploadAll') }}
              </el-button>
              <input ref="fileInput" type="file" multiple class="ftw-file-input" @change="onFilesChosen" />
            </div>
            <div class="ftw-drop">
              <div v-if="!staging.length" class="ftw-drop-hint">{{ t('ft.dropHint') }}</div>
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

        <!-- 右栏:远端(也是本地面板/OS 拖入的上传落点) -->
        <div
          class="ftw-pane"
          :class="{ 'drag-over': remoteDragOver }"
          @dragover.prevent="remoteDragOver = true"
          @dragleave.prevent="remoteDragOver = false"
          @drop.prevent="onRemoteDrop"
        >
          <div class="ftw-pane-header">
            <span class="ftw-pane-title">{{ t('ft.remote') }}</span>
            <el-button size="small" :disabled="!ft.ftReady.value || ft.ftLoading.value" @click="ft.up()">
              <el-icon><IconArrowLeft /></el-icon>&nbsp;{{ t('ft.up') }}
            </el-button>
            <el-input
              v-model="ft.ftPath.value"
              size="small"
              class="ftw-path-input"
              :disabled="!ft.ftReady.value"
              @keyup.enter="ft.refresh()"
            />
            <el-button size="small" :loading="ft.ftLoading.value" :disabled="!ft.ftReady.value" @click="ft.refresh()">
              <el-icon><IconRefresh /></el-icon>&nbsp;{{ t('ft.refresh') }}
            </el-button>
            <el-button size="small" :disabled="!ft.ftReady.value" @click="onCreateFolder">
              <el-icon><IconFolderPlus /></el-icon>&nbsp;{{ t('ft.createFolder') }}
            </el-button>
          </div>
          <el-alert v-if="ft.ftError.value" :title="ft.ftError.value" type="error" :closable="false" class="ftw-error" />
          <div v-loading="ft.ftLoading.value" class="ftw-local-list">
            <div
              v-for="f in ft.ftFiles.value"
              :key="f.name"
              class="ftw-local-item"
              draggable="true"
              @dragstart="onRemoteDragStart(f)"
              @dragend="onRemoteDragEnd"
              @dblclick="onRowDblClick(f)"
            >
              <el-icon class="ftw-entry-icon">
                <IconDeviceDesktop v-if="f.type === FT_TYPE_DRIVE" />
                <IconFolder v-else-if="isRemoteDir(f.type)" />
                <IconFile v-else />
              </el-icon>
              <span class="ftw-staging-name" :title="f.path">{{ f.name }}</span>
              <span class="ftw-staging-size">{{ isRemoteFile(f.type) ? ft.fmtSize(f.size) : '-' }}</span>
              <span class="ftw-staging-date">{{ fmtDate(f.modifiedTime) }}</span>
              <el-button size="small" link type="primary" :title="t('ft.download')" @click="onDownload(f)">
                <el-icon><IconDownload /></el-icon>
              </el-button>
              <el-button size="small" link type="primary" :title="t('ft.rename')" @click="onRename(f)">
                <el-icon><IconEdit /></el-icon>
              </el-button>
              <el-button size="small" link type="danger" :title="t('ft.delete')" @click="onDelete(f)">
                <el-icon><IconTrash /></el-icon>
              </el-button>
            </div>
            <div v-if="!ft.ftFiles.value.length && !ft.ftLoading.value" class="ftw-drop-hint">{{ t('ft.remoteEmpty') }}</div>
          </div>
        </div>
      </div>

      <!-- 下半部:传输队列 -->
      <div class="ftw-bottom">
        <div class="ftw-stats">
          <span>{{ t('ft.stats', { total: statTotal, done: statDone, failed: statFailed }) }}</span>
          <span class="ftw-speed up"><el-icon><IconUpload /></el-icon> {{ fmtSpeed(upSpeed) }}</span>
          <span class="ftw-speed down"><el-icon><IconDownload /></el-icon> {{ fmtSpeed(downSpeed) }}</span>
          <el-button size="small" class="ftw-clear" :disabled="!statDone && !statFailed" @click="ft.clearFinished()">
            {{ t('ft.clearFinished') }}
          </el-button>
        </div>
        <div class="ftw-table-wrap">
          <el-table :data="jobs" size="small" height="100%">
            <el-table-column :label="t('ft.direction')" width="80">
              <template #default="{ row }">
                <span class="ftw-direction">
                  <el-icon><IconUpload v-if="row.direction === 'upload'" /><IconDownload v-else /></el-icon>
                  {{ row.direction === 'upload' ? t('ft.upload') : t('ft.downloadDir') }}
                </span>
              </template>
            </el-table-column>
            <el-table-column :label="t('ft.fileName')" min-width="160">
              <template #default="{ row }">
                <span class="ftw-file-name" :title="row.remotePath">{{ row.displayName }}</span>
              </template>
            </el-table-column>
            <el-table-column :label="t('ft.size')" width="90">
              <template #default="{ row }">
                {{ row.totalSize > 0 ? ft.fmtSize(row.totalSize) : '-' }}
              </template>
            </el-table-column>
            <el-table-column :label="t('ft.progress')" min-width="150">
              <template #default="{ row }">
                <el-progress
                  :percentage="jobPercent(row)"
                  :status="row.state === 'done' ? 'success' : row.state === 'error' || row.state === 'cancelled' ? 'exception' : undefined"
                />
              </template>
            </el-table-column>
            <el-table-column :label="t('ft.files')" width="90">
              <template #default="{ row }">
                {{ row.fileCount ? `${Math.min(row.fileNum + 1, row.fileCount)}/${row.fileCount}` : '-' }}
              </template>
            </el-table-column>
            <el-table-column :label="t('ft.speed')" width="100">
              <template #default="{ row }">
                {{ row.state === 'running' ? fmtSpeed(row.speedBps) : '-' }}
              </template>
            </el-table-column>
            <el-table-column :label="t('ft.state')" width="90">
              <template #default="{ row }">
                <span :class="{ 'ftw-state-error': row.state === 'error' }" :title="row.error ?? ''">
                  {{ jobStateText(row) }}
                </span>
              </template>
            </el-table-column>
            <el-table-column :label="t('ft.actions')" width="70">
              <template #default="{ row }">
                <el-button
                  v-if="row.state === 'running' || row.state === 'pending'"
                  size="small"
                  link
                  type="danger"
                  @click="ft.cancelJob(row)"
                >
                  {{ t('ft.cancel') }}
                </el-button>
              </template>
            </el-table-column>
          </el-table>
        </div>
      </div>

      <!-- 覆盖确认弹框 -->
      <el-dialog
        v-model="overwriteVisible"
        :title="t('ft.overwriteTitle')"
        width="440px"
        :close-on-click-modal="false"
        :show-close="false"
        append-to-body
      >
        <div v-if="overwriteReq" class="ftw-overwrite-body">
          <p>
            {{
              t('ft.overwriteMsg', {
                name: overwriteReq.path,
                remoteSize: ft.fmtSize(overwriteReq.remoteSize),
                localSize: overwriteReq.localSize >= 0 ? ft.fmtSize(overwriteReq.localSize) : '-',
              })
            }}
          </p>
          <el-checkbox v-model="overwriteApplyAll">{{ t('ft.applyAll') }}</el-checkbox>
        </div>
        <template #footer>
          <el-button @click="decideOverwrite('skip')">{{ t('ft.skip') }}</el-button>
          <el-button
            v-if="overwriteReq && overwriteReq.resumableBytes > 0"
            type="warning"
            @click="decideOverwrite('resume')"
          >
            {{ t('ft.resume', { size: ft.fmtSize(overwriteReq.resumableBytes) }) }}
          </el-button>
          <el-button type="primary" @click="decideOverwrite('overwrite')">{{ t('ft.overwrite') }}</el-button>
        </template>
      </el-dialog>
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
.ftw-pane.drag-over {
  outline: 2px dashed #409eff;
  outline-offset: -2px;
  background: #ecf5ff;
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
.ftw-staging-date {
  color: #909399;
  font-size: 12px;
  flex-shrink: 0;
  width: 140px;
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
.ftw-overwrite-body p {
  margin: 0 0 12px;
  font-size: 13px;
  color: #303133;
  word-break: break-all;
}
</style>
