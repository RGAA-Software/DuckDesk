<script lang="ts" setup>
import { computed, onMounted, ref, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Warning } from '@element-plus/icons-vue'
import http from '@/utils/http'

interface DeviceAuth {
  auth_id: string
  auth_name: string
  machine_code: string
  product: string
  mode: string
  days: number
  max_streams: number
  created_timestamp_ms: number
  end_timestamp_ms: number
  revoked: boolean
  client_version: string
  client_status: string
  client_os: string
  client_device_count: number
  client_reported_at_ms: number
  total: number
  // Console 的 web 登录凭据（license 携带，仅 pixels_console 使用）
  username: string
  password: string
}

const categories = [
  { key: 'gopico', label: 'GoPico' },
  { key: 'clientbox', label: 'ClientBox' },
  { key: 'goagent', label: 'GoAgent' },
  { key: 'pixels_console', label: 'Pixels Console' },
]

const activeTab = ref('gopico')

// pixels_console 的 max_streams 语义是流路数(Max Streams),其它产品是设备数(Max Devices)
const maxStreamsLabel = computed(() => (activeTab.value === 'pixels_console' ? '流路数' : '设备数'))
const tableData = ref<DeviceAuth[]>([])
const totalCount = ref(0)
const pageSize = ref(20)
const currentPage = ref(1)
const loading = ref(false)

// 错误提示
const errorMessage = ref('')
const errorDialogVisible = ref(false)
const showError = (err: any, fallback: string) => {
  errorMessage.value = err.response?.data?.message || fallback
  errorDialogVisible.value = true
}
const showMessage = (message: string) => {
  errorMessage.value = message
  errorDialogVisible.value = true
}

const queryDevices = async () => {
  loading.value = true
  try {
    const params = {
      product: activeTab.value,
      page: currentPage.value,
      page_size: pageSize.value,
    }
    const response = await http.get('/product/query/authorizations', { params })
    const items: DeviceAuth[] = response.data.data || []
    tableData.value = items
    totalCount.value = items.length > 0 ? Number(items[0].total) : 0
  } catch (err: any) {
    tableData.value = []
    totalCount.value = 0
    showError(err, '请求设备列表失败')
  } finally {
    loading.value = false
  }
}

const handleTabChange = () => {
  currentPage.value = 1
  void queryDevices()
}

const handleSizeChange = (val: number) => {
  pageSize.value = val
  void queryDevices()
}
const handleCurrentChange = (val: number) => {
  currentPage.value = val
  void queryDevices()
}

const formatTime = (ms: number) => {
  if (!ms) return '-'
  return new Date(ms).toLocaleString()
}

const leftDays = (row: DeviceAuth) => {
  if (row.mode === 'trial') return '不限'
  const left = Math.ceil((row.end_timestamp_ms - Date.now()) / 24 / 3600 / 1000)
  return left >= 0 ? left : 0
}

// ---------- 编辑授权 ----------
const editDialogVisible = ref(false)
const isSaving = ref(false)
const editForm = ref({
  auth_id: '',
  device_code: '',
  mode: 'licensed',
  days: 30,
  max_devices: 1,
  username: '',
  password: '',
})

const openEdit = (row: DeviceAuth) => {
  editForm.value = {
    auth_id: row.auth_id,
    device_code: row.machine_code,
    mode: row.mode === 'trial' ? 'trial' : 'licensed',
    days: row.mode === 'trial' ? 30 : row.days,
    max_devices: row.max_streams,
    username: row.username || '',
    password: row.password || '',
  }
  editDialogVisible.value = true
}

// 复制 Console web 登录账号（仅 pixels_console 有意义）
const copyLoginAccount = async () => {
  const text = `用户名: ${editForm.value.username}\n密码: ${editForm.value.password}`
  try {
    await navigator.clipboard.writeText(text)
    ElMessage.success('登录账号已复制')
  } catch {
    ElMessage.error('复制失败')
  }
}

const handleSave = async () => {
  if (isSaving.value) return
  if (editForm.value.mode === 'licensed') {
    if (!Number.isInteger(editForm.value.days) || editForm.value.days < 1) {
      ElMessage.error('天数必须是大于 0 的整数')
      return
    }
  }
  if (!Number.isInteger(editForm.value.max_devices) || editForm.value.max_devices < 1) {
    ElMessage.error(`${maxStreamsLabel.value}必须是大于 0 的整数`)
    return
  }
  isSaving.value = true
  try {
    await http.post('/device/update/authorization', {
      auth_id: editForm.value.auth_id,
      mode: editForm.value.mode,
      days: Number(editForm.value.days),
      max_devices: Number(editForm.value.max_devices),
    })
    ElMessage.success('已保存，设备下次拉取时生效')
    editDialogVisible.value = false
    await queryDevices()
  } catch (err: any) {
    showError(err, '保存失败')
  } finally {
    isSaving.value = false
  }
}

// ---------- 删除 ----------
const isDeleting = ref(false)
const handleDelete = async (row: DeviceAuth) => {
  if (isDeleting.value) return
  try {
    await ElMessageBox.confirm(
      `确定删除设备 ${row.machine_code} 的授权记录吗？该操作不可恢复，设备下次拉取将重新注册为试用。`,
      '删除授权',
      { confirmButtonText: '删除', cancelButtonText: '取消', type: 'warning', confirmButtonClass: 'el-button--danger' },
    )
  } catch {
    return
  }
  isDeleting.value = true
  try {
    await http.post('/device/delete/authorization', { auth_id: row.auth_id })
    ElMessage.success('已删除')
    await queryDevices()
  } catch (err: any) {
    showError(err, '删除失败')
  } finally {
    isDeleting.value = false
  }
}

// ---------- 吊销 ----------
const isRevoking = ref(false)
const handleRevoke = async (row: DeviceAuth) => {
  if (isRevoking.value) return
  try {
    await ElMessageBox.confirm(
      `确定吊销设备 ${row.machine_code} 的授权吗？设备下次拉取后将收到吊销状态。`,
      '吊销授权',
      { confirmButtonText: '吊销', cancelButtonText: '取消', type: 'warning' },
    )
  } catch {
    return
  }
  isRevoking.value = true
  try {
    await http.post('/device/revoke/authorization', { auth_id: row.auth_id })
    ElMessage.success('已吊销')
    await queryDevices()
  } catch (err: any) {
    showError(err, '吊销失败')
  } finally {
    isRevoking.value = false
  }
}

watch(activeTab, handleTabChange)

onMounted(async () => {
  await queryDevices()
})
</script>

<template>
  <!-- 错误提示对话框 -->
  <el-dialog v-model="errorDialogVisible" title="" width="400">
    <div class="error-message">
      <el-icon color="#F56C6C" size="20">
        <Warning />
      </el-icon>
      <span style="margin-left: 10px;">{{ errorMessage }}</span>
    </div>
    <template #footer>
      <el-button type="primary" @click="errorDialogVisible = false">确定</el-button>
    </template>
  </el-dialog>

  <div class="device-page">
    <div class="page-head">
      <div>
        <div class="page-title">GoXR 设备授权</div>
        <div class="page-sub">设备首次拉取自动注册（试用），在此设置授权后由设备定时拉取生效</div>
      </div>
      <el-button :loading="loading" @click="queryDevices">刷新</el-button>
    </div>

    <el-tabs v-model="activeTab" class="category-tabs">
      <el-tab-pane v-for="c in categories" :key="c.key" :label="c.label" :name="c.key" />
    </el-tabs>

    <el-table :data="tableData" v-loading="loading" stripe style="width: 100%">
      <el-table-column label="设备码" prop="machine_code" :min-width="150" show-overflow-tooltip />
      <el-table-column label="授权模式" :min-width="80">
        <template #default="scope">
          <el-tag v-if="scope.row.mode === 'trial'" type="warning" size="small">试用</el-tag>
          <el-tag v-else type="success" size="small">正式</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="剩余/天数" :min-width="80">
        <template #default="scope">
          {{ leftDays(scope.row) }}<span v-if="scope.row.mode !== 'trial'"> / {{ scope.row.days }}</span>
        </template>
      </el-table-column>
      <el-table-column :label="maxStreamsLabel" prop="max_streams" :min-width="60" />
      <el-table-column label="到期时间" :min-width="130">
        <template #default="scope">
          {{ scope.row.mode === 'trial' ? '不限' : formatTime(scope.row.end_timestamp_ms) }}
        </template>
      </el-table-column>
      <el-table-column label="客户端" :min-width="90">
        <template #default="scope">
          <span v-if="scope.row.client_version">{{ scope.row.client_version }} / {{ scope.row.client_os || '-' }}</span>
          <span v-else>-</span>
        </template>
      </el-table-column>
      <el-table-column label="最近在线" :min-width="130">
        <template #default="scope">{{ formatTime(scope.row.client_reported_at_ms) }}</template>
      </el-table-column>
      <el-table-column label="状态" :min-width="70">
        <template #default="scope">
          <el-tag v-if="scope.row.revoked" type="danger" size="small">已吊销</el-tag>
          <el-tag v-else type="success" size="small">有效</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="操作" :min-width="210" fixed="right">
        <template #default="scope">
          <el-button size="small" type="primary" @click="openEdit(scope.row)">编辑授权</el-button>
          <el-button
            v-if="!scope.row.revoked"
            size="small"
            type="danger"
            :loading="isRevoking"
            @click="handleRevoke(scope.row)"
          >
            吊销
          </el-button>
          <el-button
            size="small"
            type="danger"
            plain
            :loading="isDeleting"
            @click="handleDelete(scope.row)"
          >
            删除
          </el-button>
        </template>
      </el-table-column>
      <template #empty>
        <span>暂无设备，等待设备上线拉取后自动注册</span>
      </template>
    </el-table>

    <div class="flex justify-center">
      <el-pagination
        v-model:current-page="currentPage"
        v-model:page-size="pageSize"
        :page-sizes="[20, 40, 60, 80]"
        layout="total, sizes, prev, pager, next, jumper"
        :total="totalCount"
        @size-change="handleSizeChange"
        @current-change="handleCurrentChange"
      />
    </div>
  </div>

  <!-- 编辑授权对话框 -->
  <el-dialog v-model="editDialogVisible" title="编辑设备授权" width="480">
    <el-form label-width="90px">
      <el-form-item label="设备码">
        <el-input :model-value="editForm.device_code" disabled />
      </el-form-item>
      <el-form-item label="授权模式">
        <el-radio-group v-model="editForm.mode">
          <el-radio value="trial">试用</el-radio>
          <el-radio value="licensed">正式</el-radio>
        </el-radio-group>
      </el-form-item>
      <el-form-item v-if="editForm.mode === 'licensed'" label="天数">
        <el-input-number v-model="editForm.days" :min="1" :max="365000" />
      </el-form-item>
      <el-form-item :label="maxStreamsLabel">
        <el-input-number v-model="editForm.max_devices" :min="1" :max="100000" />
      </el-form-item>
      <el-form-item v-if="editForm.mode === 'trial'" label="">
        <span class="trial-tip">试用模式不限时间，仅受{{ maxStreamsLabel }}限制</span>
      </el-form-item>
      <template v-if="activeTab === 'pixels_console'">
        <el-form-item label="登录用户名">
          <el-input :model-value="editForm.username" disabled />
        </el-form-item>
        <el-form-item label="登录密码">
          <el-input :model-value="editForm.password" disabled />
        </el-form-item>
        <el-form-item label="">
          <el-button size="small" @click="copyLoginAccount">复制登录账号</el-button>
          <span class="trial-tip" style="margin-left: 8px;">用于登录 Console web 管理页</span>
        </el-form-item>
      </template>
    </el-form>
    <template #footer>
      <el-button @click="editDialogVisible = false">取消</el-button>
      <el-button type="primary" :loading="isSaving" :disabled="isSaving" @click="handleSave">
        保存
      </el-button>
    </template>
  </el-dialog>
</template>

<style scoped>
.device-page {
  max-width: 1500px;
}
.page-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 6px;
}
.page-title {
  font-size: 18px;
  font-weight: 700;
  color: var(--gd-text-1);
  letter-spacing: 0.02em;
}
.page-sub {
  margin-top: 4px;
  font-size: 13px;
  color: var(--gd-text-2);
}
.category-tabs {
  margin-bottom: 4px;
}
.category-tabs :deep(.el-tabs__item) {
  color: var(--gd-text-2);
  font-weight: 500;
}
.category-tabs :deep(.el-tabs__item.is-active) {
  color: var(--gd-cyan);
}
.category-tabs :deep(.el-tabs__active-bar) {
  background: var(--gd-gradient);
}
.category-tabs :deep(.el-tabs__nav-wrap::after) {
  background: var(--gd-line);
  height: 1px;
}
.trial-tip {
  font-size: 12px;
  color: var(--gd-text-2);
}
</style>
