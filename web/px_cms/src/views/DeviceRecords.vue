<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { notification } from 'ant-design-vue'
import { BASE_URL } from '@/http.ts'
import { formatTimestamp } from '@/util/time.ts'
import {
  RecordApiError,
  RECORD_ERR_DEVICE_OFFLINE,
  RECORD_ERR_TIMEOUT,
  deleteRecord,
  downloadRecordToCms,
  fetchRecord,
  getRecordAccess,
  getRecordList,
  getRecordTicket,
  type RecordFileItem,
  type RecordTicket,
} from '@/model/record_api.ts'

const route = useRoute()
const router = useRouter()
const deviceId = route.params.device_id as string

// topology: '' = detecting, 'direct' = topology 1 (panel LAN direct), 'cms' = topology 2 (via cms)
const topology = ref<'' | 'direct' | 'cms'>('')
const loading = ref(true)
const loadError = ref('')
const files = ref<RecordFileItem[]>([])

// topology 1 state
let directBase = ''
let directIp = ''
// names present in the panel-side list; cms-only copies (rotated out on the
// device) are not in it and must be played/downloaded from the cms url
let panelNames = new Set<string>()

// polling for fetching / downloading progress
let pollTimer: ReturnType<typeof setInterval> | null = null

const formatSize = (size: number): string => {
  if (size >= 1024 * 1024 * 1024) return (size / 1024 / 1024 / 1024).toFixed(2) + ' GB'
  if (size >= 1024 * 1024) return (size / 1024 / 1024).toFixed(2) + ' MB'
  if (size >= 1024) return (size / 1024).toFixed(2) + ' KB'
  return size + ' B'
}

const fetchPercent = (file: RecordFileItem): number => {
  if (file.total <= 0) return 0
  return Math.min(99, Math.floor((file.progress / file.total) * 100))
}

// codec == '' means the cms-side copy without probe info; default encoder is h264
const isPlayable = (file: RecordFileItem): boolean => {
  return file.codec === '' || file.codec === 'h264'
}

const stateText = (file: RecordFileItem): string => {
  switch (file.state) {
    case 'fetching':
      return `回传中 ${fetchPercent(file)}%`
    case 'ready':
      return file.keep ? '已存 CMS' : '已回传'
    case 'error':
      return '回传失败'
    default:
      return '设备本地'
  }
}

const stateColor = (file: RecordFileItem): string => {
  switch (file.state) {
    case 'fetching':
      return 'processing'
    case 'ready':
      return file.keep ? 'success' : 'default'
    case 'error':
      return 'error'
    default:
      return 'default'
  }
}

const showApiError = (prefix: string, e: unknown) => {
  if (e instanceof RecordApiError) {
    if (e.code === RECORD_ERR_DEVICE_OFFLINE) {
      notification.error({ message: prefix + '：设备离线，无法查看录像' })
      return
    }
    if (e.code === RECORD_ERR_TIMEOUT) {
      notification.error({ message: prefix + '：设备响应超时' })
      return
    }
    notification.error({ message: prefix + '：' + e.message })
    return
  }
  notification.error({ message: prefix })
}

const loadErrorText = (e: unknown): string => {
  if (e instanceof RecordApiError) {
    if (e.code === RECORD_ERR_DEVICE_OFFLINE) return '设备离线，无法查看录像'
    if (e.code === RECORD_ERR_TIMEOUT) return '设备响应超时'
    return e.message
  }
  return '加载录像列表失败'
}

// probe one panel lan ip: GET /records/info with the "*" ticket, 2s timeout (design 5.3)
const probePanel = async (ip: string, port: number, ticket: RecordTicket): Promise<boolean> => {
  try {
    const resp = await fetch(
      `http://${ip}:${port}/records/info?tk=${ticket.tk}&exp=${ticket.exp}`,
      { signal: AbortSignal.timeout(2000) },
    )
    return resp.ok
  } catch {
    return false
  }
}

// topology 1: list straight from the panel (design 5.2)
const loadDirectList = async () => {
  const ticket = await getRecordTicket(deviceId, '*')
  const resp = await fetch(`${directBase}/records?tk=${ticket.tk}&exp=${ticket.exp}`)
  if (!resp.ok) {
    throw new RecordApiError(resp.status, 'panel records list failed: ' + resp.status)
  }
  const data = await resp.json()
  const list: RecordFileItem[] = (data.files || []).map(
    (f: { name: string; size: number; mtime: number; monitor: string; codec: string }) => ({
      id: '',
      name: f.name,
      size: f.size,
      mtime: f.mtime,
      monitor: f.monitor || '',
      codec: f.codec || '',
      state: 'none',
      keep: false,
      progress: 0,
      total: 0,
      url: '',
    }),
  )
  files.value = list
  panelNames = new Set(list.map((f) => f.name))
  // best effort: merge the cms-side download state (keep/fetching progress)
  await mergeCmsState()
}

// merge cms-side c_records state into the current list (by filename)
const mergeCmsState = async () => {
  try {
    const resp = await getRecordList(deviceId)
    const byName = new Map(files.value.map((f) => [f.name, f]))
    for (const item of resp.files) {
      const existing = byName.get(item.name)
      if (existing) {
        existing.id = item.id
        existing.state = item.state
        existing.keep = item.keep
        existing.progress = item.progress
        existing.total = item.total
        existing.url = item.url
        byName.delete(item.name)
      } else {
        // cms-only copy (the device file may have been rotated out)
        files.value.push(item)
      }
    }
  } catch (e) {
    // tunnel down etc: keep the panel list as-is
    console.warn('mergeCmsState failed', e)
  }
}

// topology 2: cms merged list (design 6.3)
const loadCmsList = async () => {
  const resp = await getRecordList(deviceId)
  files.value = resp.files
}

const refreshList = async () => {
  if (topology.value === 'direct') {
    await loadDirectList()
  } else if (topology.value === 'cms') {
    await loadCmsList()
  }
}

const hasInFlight = (): boolean => files.value.some((f) => f.state === 'fetching')

const stopPolling = () => {
  if (pollTimer !== null) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

// poll the cms list while any file is fetching, to show "回传中 x%" (design 6.1)
const ensurePolling = () => {
  if (pollTimer !== null) return
  pollTimer = setInterval(async () => {
    if (!hasInFlight()) {
      stopPolling()
      return
    }
    try {
      if (topology.value === 'direct') {
        await mergeCmsState()
      } else {
        await loadCmsList()
      }
      updateFetchProgress()
    } catch (e) {
      console.warn('poll record list failed', e)
    }
  }, 1500)
}

const init = async () => {
  loading.value = true
  loadError.value = ''
  stopPolling()
  try {
    const access = await getRecordAccess(deviceId)
    // topology selection (design 5.3): try panel lan ips first, fall back to cms
    if (access.online && access.panel_lan_ips.length > 0) {
      try {
        const ticket = await getRecordTicket(deviceId, '*')
        const port = access.panel_port > 0 ? access.panel_port : 20369
        for (const ip of access.panel_lan_ips) {
          if (await probePanel(ip, port, ticket)) {
            topology.value = 'direct'
            directBase = `http://${ip}:${port}`
            directIp = ip
            break
          }
        }
      } catch (e) {
        console.warn('record ticket failed, fall back to cms topology', e)
      }
    }
    if (topology.value === 'direct') {
      await loadDirectList()
    } else {
      topology.value = 'cms'
      await loadCmsList()
    }
    ensurePolling()
  } catch (e) {
    console.error('init records page failed', e)
    loadError.value = loadErrorText(e)
  } finally {
    loading.value = false
  }
}

const handleRefresh = async () => {
  loading.value = true
  loadError.value = ''
  try {
    await refreshList()
    ensurePolling()
  } catch (e) {
    loadError.value = loadErrorText(e)
  } finally {
    loading.value = false
  }
}

// ---------------- play ----------------

const playDialogVisible = ref(false)
const playUrl = ref('')
const playTitle = ref('')

const openPlayer = (url: string, name: string) => {
  playUrl.value = url
  playTitle.value = name
  playDialogVisible.value = true
}

const handlePlay = async (file: RecordFileItem) => {
  // cms-only copy (rotated out on the device): play from the cms url directly
  if (topology.value === 'direct' && !panelNames.has(file.name)) {
    if (file.state === 'ready' && file.url) {
      openPlayer(BASE_URL + file.url, file.name)
    }
    return
  }
  if (topology.value === 'direct') {
    try {
      const ticket = await getRecordTicket(deviceId, file.name)
      openPlayer(`${directBase}/records/${encodeURIComponent(file.name)}?tk=${ticket.tk}&exp=${ticket.exp}`, file.name)
    } catch (e) {
      showApiError('获取播放地址失败', e)
    }
    return
  }
  await ensureCmsReady(file, (url) => openPlayer(url, file.name))
}

// ---------------- download to local ----------------

const downloadLocal = (url: string, name: string) => {
  const a = document.createElement('a')
  a.href = url
  a.download = name
  a.rel = 'noopener'
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
}

const handleDownloadLocal = async (file: RecordFileItem) => {
  // cms-only copy (rotated out on the device): download from the cms url
  if (topology.value === 'direct' && !panelNames.has(file.name)) {
    if (file.state === 'ready' && file.url) {
      downloadLocal(BASE_URL + file.url, file.name)
    }
    return
  }
  if (topology.value === 'direct') {
    try {
      const ticket = await getRecordTicket(deviceId, file.name)
      downloadLocal(
        `${directBase}/records/${encodeURIComponent(file.name)}?tk=${ticket.tk}&exp=${ticket.exp}`,
        file.name,
      )
    } catch (e) {
      showApiError('获取下载地址失败', e)
    }
    return
  }
  await ensureCmsReady(file, (url) => downloadLocal(url, file.name))
}

// ---------------- topology 2: fetch then ready ----------------

const fetchDialogVisible = ref(false)
const fetchDialogName = ref('')
const fetchDialogPercent = ref(0)
let fetchDialogTarget: RecordFileItem | null = null
let fetchDialogOnReady: ((url: string) => void) | null = null

// track the fetch progress of the dialog target from the polled list
const updateFetchProgress = () => {
  if (!fetchDialogVisible.value || !fetchDialogTarget) return
  const current = files.value.find((f) => f.name === fetchDialogTarget!.name)
  if (!current) return
  if (current.state === 'ready' && current.url) {
    const onReady = fetchDialogOnReady
    fetchDialogVisible.value = false
    fetchDialogTarget = null
    fetchDialogOnReady = null
    if (onReady) onReady(BASE_URL + current.url)
  } else if (current.state === 'error') {
    fetchDialogVisible.value = false
    fetchDialogTarget = null
    fetchDialogOnReady = null
    notification.error({ message: '录像回传失败，请重试' })
  } else {
    fetchDialogPercent.value = fetchPercent(current)
  }
}

// cms topology: trigger /fetch and wait until the file is ready on cms,
// showing the "回传中 x%" progress so the first-play delay is explicit (design 6.1)
const ensureCmsReady = async (file: RecordFileItem, onReady: (url: string) => void) => {
  if (file.state === 'ready' && file.url) {
    onReady(BASE_URL + file.url)
    return
  }
  try {
    const resp = await fetchRecord(deviceId, file.name)
    if (resp.state === 'ready' && resp.url) {
      onReady(BASE_URL + resp.url)
      return
    }
  } catch (e) {
    showApiError('触发录像回传失败', e)
    return
  }
  file.state = 'fetching'
  fetchDialogTarget = file
  fetchDialogOnReady = onReady
  fetchDialogName.value = file.name
  fetchDialogPercent.value = fetchPercent(file)
  fetchDialogVisible.value = true
  ensurePolling()
}

const handleFetchDialogCancel = () => {
  // cancel only closes the dialog; the cms-side fetch continues
  fetchDialogVisible.value = false
  fetchDialogTarget = null
  fetchDialogOnReady = null
}

// ---------------- download to cms / delete ----------------

const downloadingNames = ref<Set<string>>(new Set())

const handleDownloadCms = async (file: RecordFileItem) => {
  downloadingNames.value.add(file.name)
  try {
    const resp = await downloadRecordToCms(deviceId, file.name)
    file.id = resp.id
    file.keep = true
    if (resp.state === 'ready') {
      file.state = 'ready'
      file.url = resp.url
      notification.success({ message: '已保存到 CMS' })
    } else {
      file.state = 'fetching'
      notification.info({ message: '正在下载到 CMS，可在列表查看进度' })
      ensurePolling()
    }
  } catch (e) {
    showApiError('下载到 CMS 失败', e)
  } finally {
    downloadingNames.value.delete(file.name)
  }
}

const handleDelete = async (file: RecordFileItem) => {
  if (!file.id) return
  try {
    await deleteRecord(file.id)
    notification.success({ message: '已删除 CMS 上的副本' })
    await handleRefresh()
  } catch (e) {
    showApiError('删除失败', e)
  }
}

const handleBack = () => {
  router.push('/devices-list')
}

onMounted(init)

onUnmounted(() => {
  stopPolling()
})
</script>

<template>
  <div class="w-full">
    <a-card class="w-full" :bordered="false">
      <template #title>
        <div class="flex items-center">
          <a-button size="small" @click="handleBack">返回</a-button>
          <div class="w-3" />
          <span class="text-lg font-bold text-slate-800">设备录像</span>
          <div class="w-3" />
          <span class="!font-bold">{{ deviceId }}</span>
          <div class="w-3" />
          <a-tag v-if="topology === 'direct'" color="success">直连设备 {{ directIp }}</a-tag>
          <a-tag v-else-if="topology === 'cms'" color="warning">经 CMS 回传</a-tag>
        </div>
      </template>
      <template #extra>
        <a-button size="small" :loading="loading" @click="handleRefresh">刷新</a-button>
      </template>

      <a-alert
        v-if="loadError"
        type="error"
        :message="loadError"
        show-icon
        class="!mb-3"
      />

      <a-table :data-source="files" :loading="loading" style="width: 100%" row-key="name">
        <a-table-column title="文件名" :min-width="220">
          <template #default="{ record }">
            <div class="flex flex-col">
              <span class="!font-bold">{{ record.name }}</span>
              <span v-if="record.monitor" class="text-xs text-slate-500">
                监视器: {{ record.monitor }}
              </span>
            </div>
          </template>
        </a-table-column>

        <a-table-column title="大小" :min-width="80">
          <template #default="{ record }">
            <span>{{ formatSize(record.size) }}</span>
          </template>
        </a-table-column>

        <a-table-column title="修改时间" :min-width="120">
          <template #default="{ record }">
            <span>{{ record.mtime ? formatTimestamp(record.mtime * 1000) : '-' }}</span>
          </template>
        </a-table-column>

        <a-table-column title="编码" :min-width="90">
          <template #default="{ record }">
            <span>{{ record.codec || 'h264' }}</span>
            <a-tag v-if="!isPlayable(record)" color="warning" class="!ml-1">仅可下载</a-tag>
          </template>
        </a-table-column>

        <a-table-column title="状态" :min-width="110">
          <template #default="{ record }">
            <a-tag :color="stateColor(record)">{{ stateText(record) }}</a-tag>
          </template>
        </a-table-column>

        <a-table-column title="操作" :min-width="260">
          <template #default="{ record }">
            <a-button
              v-if="isPlayable(record)"
              size="small"
              type="primary"
              @click="handlePlay(record)"
            >
              播放
            </a-button>

            <a-button size="small" @click="handleDownloadLocal(record)">下载到本机</a-button>

            <template v-if="record.keep && record.state === 'ready'">
              <a-tag color="success">已存 CMS</a-tag>
              <a-popconfirm
                title="确定删除 CMS 上的副本吗？设备本地录像不受影响。"
                ok-text="删除"
                cancel-text="取消"
                @confirm="handleDelete(record)"
              >
                <a-button size="small" type="danger">删除</a-button>
              </a-popconfirm>
            </template>
            <a-button
              v-else
              size="small"
              :loading="downloadingNames.has(record.name)"
              :disabled="record.state === 'fetching'"
              @click="handleDownloadCms(record)"
            >
              下载到 CMS
            </a-button>
          </template>
        </a-table-column>
      </a-table>
    </a-card>

    <!-- player -->
    <a-modal
      v-model:open="playDialogVisible"
      :title="playTitle"
      :mask="false"
      centered
      destroy-on-close
      class="!w-250"
      :footer="null"
    >
      <video :src="playUrl" controls autoplay style="width: 100%; background: #000" />
    </a-modal>

    <!-- topology 2 fetch progress: first-play delay = full transfer time (design 6.1) -->
    <a-modal
      :open="fetchDialogVisible"
      title="录像回传中"
      :closable="true"
      :mask-closable="false"
      centered
      :footer="null"
      @cancel="handleFetchDialogCancel"
    >
      <div class="flex flex-col items-start">
        <span>{{ fetchDialogName }}</span>
        <div class="h-2" />
        <a-progress :percent="fetchDialogPercent" status="active" style="width: 100%" />
        <div class="h-2" />
        <span class="text-xs text-slate-500">
          首次播放需要先把整段录像回传到 CMS，大文件可能需要几分钟，请耐心等待。
        </span>
      </div>
    </a-modal>
  </div>
</template>

<style scoped></style>
