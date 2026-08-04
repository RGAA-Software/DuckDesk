// 独立文件传输窗口:对齐 C++ 端 src/gr_client/plugins/file_transfer_client/widget
// 上半部:本地暂存区(左) / 远端文件(右) 双栏;下半部:统计条 + 传输记录表格
<script setup lang="ts">
import { computed, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import type { FileTransferApi } from './useFileTransfer'
import type { RemoteFileInfo, TransferTask } from './rtc/file_transfer'

const props = defineProps<{
  deviceId: string
  ft: FileTransferApi
}>()

const visible = defineModel<boolean>('visible', { required: true })

const title = computed(() => `文件传输[web_${props.deviceId}]`)

// ---------- 本地暂存区 ----------
interface StagingItem {
  id: number
  file: File
}
const staging = ref<StagingItem[]>([])
let stagingSeq = 0
const fileInput = ref<HTMLInputElement | null>(null)
const dragOver = ref(false)
const uploading = ref(false)

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

// ---------- 远端文件 ----------
function fmtDate(date: number): string {
  if (!date) return '-'
  return new Date(date * 1000).toLocaleString()
}

function fileIcon(item: RemoteFileInfo): string {
  if (item.type === 0) return '💻'
  if (item.type === 2) return '📄'
  return '📁'
}

// 双击:文件夹/盘符进目录,文件直接下载
function onRowDblClick(row: RemoteFileInfo) {
  if (row.type === 2) {
    void props.ft.downloadAndSave(row)
  } else {
    props.ft.enter(row)
  }
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

function fmtSpeed(bps: number): string {
  return bps > 0 ? `${props.ft.fmtSize(bps)}/s` : '-'
}
</script>

<template>
  <div v-if="visible" class="ftw-mask">
    <div class="ftw-window">
      <!-- 标题栏 -->
      <div class="ftw-titlebar">
        <span class="ftw-title">{{ title }}</span>
        <button class="ftw-close" title="关闭" @click="visible = false">×</button>
      </div>

      <!-- 上半部:本地暂存区 / 远端文件 双栏 -->
      <div class="ftw-main">
        <div class="ftw-pane">
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
              <span class="ftw-staging-name" :title="s.file.name">📄 {{ s.file.name }}</span>
              <span class="ftw-staging-size">{{ ft.fmtSize(s.file.size) }}</span>
              <el-button size="small" link type="danger" @click="removeStaging(s.id)">×</el-button>
            </div>
          </div>
        </div>

        <div class="ftw-divider"></div>

        <div class="ftw-pane">
          <div class="ftw-pane-header">
            <span class="ftw-pane-title">远端</span>
            <el-button size="small" :disabled="!ft.ftReady.value || ft.ftLoading.value" @click="ft.up()">
              上级
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
              刷新
            </el-button>
            <el-button size="small" :disabled="!ft.ftReady.value" @click="onCreateFolder">
              新建文件夹
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
                <span class="ftw-file-name" :title="row.path">{{ fileIcon(row) }} {{ row.name }}</span>
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
                <el-button size="small" link type="primary" @click="ft.downloadAndSave(row)">下载</el-button>
                <el-button size="small" link type="primary" @click="onRename(row)">重命名</el-button>
                <el-button size="small" link type="danger" @click="onDelete(row)">删除</el-button>
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
          <span class="ftw-speed up">↗ {{ fmtSpeed(upSpeed) }}</span>
          <span class="ftw-speed down">↙ {{ fmtSpeed(downSpeed) }}</span>
          <el-button size="small" class="ftw-clear" :disabled="!statDone && !statFailed" @click="ft.clearFinished()">
            清除已完成
          </el-button>
        </div>
        <div class="ftw-table-wrap">
          <el-table :data="tasks" size="small" height="100%">
          <el-table-column label="方向" width="70">
            <template #default="{ row }">
              {{ row.direction === 'upload' ? '↑ 上传' : '↓ 下载' }}
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
  font-size: 20px;
  width: 32px;
  height: 32px;
  cursor: pointer;
  border-radius: 4px;
  line-height: 1;
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
</style>
