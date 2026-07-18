<template>

  <!-- 错误提示对话框 -->
  <el-dialog
      v-model="errorDialogVisible"
      title=""
      width="400"
  >
    <div class="error-message">
      <el-icon color="#F56C6C" size="20">
        <Warning />
      </el-icon>
      <span style="margin-left: 10px;">{{ errorMessage }}</span>
    </div>

    <template #footer>
      <div class="dialog-footer">
        <el-button type="primary" @click="errorDialogVisible = false">
          确定
        </el-button>
      </div>
    </template>
  </el-dialog>

  <div class="flex items-center gap-2 p-2">
    <el-button type="primary" @click="handleOpenCreate">新建授权</el-button>
    <el-input
      v-model="search"
      size="default"
      placeholder="按名称搜索"
      clearable
      style="width: 240px"
    />
  </div>

  <el-table :data="filterTableData" stripe style="width: 100%">
    <el-table-column label="Auth ID" prop="auth_id" :min-width="100" />
    <el-table-column label="名称" prop="auth_name" :min-width="60"/>
    <el-table-column label="Machine Code" prop="machine_code" :min-width="120" />
    <el-table-column label="创建时间" prop="created_timestamp_ms" :formatter="formatDateCreateTime"/>
    <el-table-column label="到期时间" prop="end_timestamp_ms" :formatter="formatDateEndTime"/>
    <el-table-column label="授权天数" prop="days" />
    <el-table-column label="最大设备数" prop="max_streams" />
    <el-table-column label="状态" :min-width="70">
      <template #default="scope">
        <el-tag v-if="scope.row.revoked" type="danger" size="small">已吊销</el-tag>
        <el-tag v-else type="success" size="small">有效</el-tag>
      </template>
    </el-table-column>
    <el-table-column label="客户端版本" :min-width="80">
      <template #default="scope">
        {{ scope.row.client_version || '-' }}
      </template>
    </el-table-column>
    <el-table-column label="客户端状态" :min-width="80">
      <template #default="scope">
        {{ scope.row.client_status || '-' }}
      </template>
    </el-table-column>
    <el-table-column label="在线设备数" prop="client_device_count" :min-width="80" />
    <el-table-column label="最近上报时间" :formatter="formatReportedTime" :min-width="110"/>
    <el-table-column align="left" :min-width="280" label="操作">
      <template #default="scope">
        <el-button size="small" @click="handleModifyInfo(scope.row)">
          修改
        </el-button>
        <el-button
          size="small"
          type="danger"
          :disabled="scope.row.revoked"
          @click="handleRevoke(scope.row)"
        >
          吊销
        </el-button>
        <el-button size="small" @click="handleCopyDeploy(scope.row)">
          复制Deploy
        </el-button>
        <el-button size="small" @click="handleDownloadDeploy(scope.row)">
          下载Deploy
        </el-button>
      </template>
    </el-table-column>
  </el-table>

  <div class="flex justify-center">
    <el-pagination
      v-model:current-page="currentPage"
      v-model:page-size="pageSize"
      :page-sizes="[20, 40, 60, 80]"
      :size="size"
      :disabled="disabled"
      :background="background"
      layout="total, sizes, prev, pager, next, jumper"
      :total="totalAuthCount"
      @size-change="handleSizeChange"
      @current-change="handleCurrentChange"
    />
  </div>

  <!-- 修改授权对话框 -->
  <el-dialog v-model="dialogVisible" title="修改授权" width="30%">
    <div v-if="selectedData">
      <el-form :model="selectedData" label-width="auto" style="max-width: 800px">
        <!-- Auth ID -->
        <el-form-item label="Auth ID">
          <el-input v-model="selectedData.auth_id" disabled></el-input>
        </el-form-item>

        <!-- 名称 -->
        <el-form-item label="名称">
          <el-input v-model="selectedData.auth_name" disabled></el-input>
        </el-form-item>

        <!-- Machine Code -->
        <el-form-item label="Machine Code">
          <el-input v-model="selectedData.machine_code" disabled></el-input>
        </el-form-item>

        <!-- Days (下拉框选择) -->
        <el-form-item label="Days">
          <el-select v-model="selectedData.days" placeholder="请选择">
            <el-option label="7" value="7"></el-option>
            <el-option label="30" value="30"></el-option>
            <el-option label="365" value="365"></el-option>
            <el-option label="365000" value="365000"></el-option>
          </el-select>
        </el-form-item>

        <el-form-item label="Max Devices">
          <el-input v-model="selectedData.max_streams"></el-input>
        </el-form-item>

        <!-- user role -->
        <el-form-item label="Customer Role">
          <el-select v-model="selectedData.role">
            <el-option label="1" value="1"></el-option>
            <el-option label="2" value="2"></el-option>
            <el-option label="3" value="3"></el-option>
          </el-select>
        </el-form-item>
      </el-form>
    </div>
    <template #footer>
      <el-button @click="dialogVisible = false">关闭</el-button>
      <el-button
        type="primary"
        :loading="isSaving"
        :disabled="isSaving"
        @click="handleSave"
      >
        保存
      </el-button>
    </template>
  </el-dialog>

  <!-- 新建授权对话框 -->
  <el-dialog v-model="createDialogVisible" title="新建 GoPico 授权" width="30%" :close-on-click-modal="false">
    <el-form label-width="auto" style="max-width: 800px">

      <!-- 用户名 -->
      <el-form-item label="User Name">
        <el-input v-model="createForm.name"></el-input>
      </el-form-item>

      <!-- Machine Code -->
      <el-form-item label="Machine Code">
        <el-input v-model="createForm.machine_code"></el-input>
      </el-form-item>

      <!-- 用户角色 -->
      <el-form-item label="Customer Role">
        <el-select v-model="createForm.role" placeholder="请选择">
          <el-option label="1" value="1"></el-option>
          <el-option label="2" value="2"></el-option>
          <el-option label="3" value="3"></el-option>
        </el-select>
      </el-form-item>

      <!-- Days (下拉框选择) -->
      <el-form-item label="Days">
        <el-select v-model="createForm.days" placeholder="请选择">
          <el-option label="7" value="7"></el-option>
          <el-option label="30" value="30"></el-option>
          <el-option label="365" value="365"></el-option>
          <el-option label="365000" value="365000"></el-option>
        </el-select>
      </el-form-item>

      <el-form-item label="Max Devices">
        <el-input v-model="createForm.max_devices" type="number" min="1" :max="MAX_AUTH_STREAMS"></el-input>
      </el-form-item>
    </el-form>
    <template #footer>
      <el-button
        type="primary"
        :loading="isCreating"
        :disabled="isCreating"
        @click="handleCreate"
      >
        创建
      </el-button>
    </template>
  </el-dialog>

  <!-- Deploy 信息对话框 -->
  <el-dialog v-model="deployDialogVisible" title="Deploy Information" width="600">
    <el-input
      v-model="deployInfo"
      type="textarea"
      :rows="8"
      readonly
    />
    <template #footer>
      <el-button @click="copyDeployInfo">复制</el-button>
      <el-button @click="downloadDeployInfo">下载</el-button>
      <el-button type="primary" @click="deployDialogVisible = false">关闭</el-button>
    </template>
  </el-dialog>

</template>

<script lang="ts" setup>
import {computed, onMounted, ref} from 'vue'
import { watch } from 'vue'
import {type ComponentSize, ElMessage, ElMessageBox} from 'element-plus'
import http from '@/utils/http'
import {
  MAX_AUTH_STREAMS,
  validateCreateAuthorization,
  validateUpdateAuthorization,
} from '@/utils/authorizationValidation'

import { useAuthStore } from '@/stores/auth'

const authStore = useAuthStore()

watch(
    () => authStore.refreshFlag,
    () => {
      queryAuthorizations(currentPage.value, pageSize.value)
    }
)

// 错误信息
const errorMessage = ref('')
const errorDialogVisible = ref(false)

interface GopicoAuthorization {
  auth_id: string
  auth_name: string
  machine_code: string
  created_timestamp_ms: number
  end_timestamp_ms: number
  last_modify_timestamp: number
  days: number
  max_streams: number
  role: number
  verify_server: string
  deploy_str: string
  product: string
  revoked: boolean
  revoked_at_ms: number
  client_version: string
  client_status: string
  client_os: string
  client_device_count: number
  client_reported_at_ms: number
  total: number
}

const search = ref('')
const tableData = ref<GopicoAuthorization[]>([])
const selectedData = ref<GopicoAuthorization | null>(null)
const dialogVisible = ref(false)
const totalAuthCount = ref(0)
const isSaving = ref(false)
const isRevoking = ref(false)
const isCreating = ref(false)

const createDialogVisible = ref(false)
const deployDialogVisible = ref(false)
const deployInfo = ref('')

const createForm = ref({
  name: '',
  machine_code: '',
  role: '',
  days: '',
  max_devices: '',
})

const filterTableData = computed(() =>
  tableData.value.filter(
    (item) =>
      !search.value ||
      item.auth_name.toLowerCase().includes(search.value.toLowerCase())
  )
)

const updateTableData = (items: GopicoAuthorization[]) => {
  tableData.value = items
  totalAuthCount.value = items.length > 0 ? Number(items[0].total) : 0
}

const showError = (err: any, fallback: string) => {
  errorMessage.value = err.response?.data?.message || fallback
  errorDialogVisible.value = true
}

const showMessage = (message: string) => {
  errorMessage.value = message
  errorDialogVisible.value = true
}

const formatDateCreateTime = (obj: GopicoAuthorization) => {
  const date = new Date(obj.created_timestamp_ms);
  return date.toLocaleString();
}

const formatDateEndTime = (obj: GopicoAuthorization) => {
  const date = new Date(obj.end_timestamp_ms);
  return date.toLocaleString();
}

const formatReportedTime = (obj: GopicoAuthorization) => {
  if (!obj.client_reported_at_ms) {
    return '-'
  }
  const date = new Date(obj.client_reported_at_ms);
  return date.toLocaleString();
}

// GET 请求函数
const queryAuthorizations = async (page: number, pageSize: number) => {
  try {
    const params = {
      page: page,
      page_size: pageSize,
    }
    const response = await http.get('/gopico/query/authorizations', {params})
    updateTableData(response.data.data || [])
  } catch (err: any) {
    tableData.value = []
    totalAuthCount.value = 0
    showError(err, '请求授权列表失败')
  }
}

const handleModifyInfo = (row: GopicoAuthorization) => {
  selectedData.value = row
  dialogVisible.value = true
}

const handleRevoke = async (row: GopicoAuthorization) => {
  if (isRevoking.value) return
  try {
    await ElMessageBox.confirm(
      `确定吊销授权「${row.auth_name}」吗？`,
      '吊销确认',
      {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning',
      }
    )
  } catch {
    // 用户取消
    return
  }

  isRevoking.value = true
  try {
    await http.post('/gopico/revoke/authorization', { auth_id: row.auth_id })
    await queryAuthorizations(currentPage.value, pageSize.value)
    showMessage('已吊销')
  } catch (err: any) {
    showError(err, '吊销失败')
  } finally {
    isRevoking.value = false
  }
}

const handleSave = async () => {
  if (!selectedData.value) return
  if (isSaving.value) return

  const validation = validateUpdateAuthorization({
    ...selectedData.value,
    product: 'gopico',
  })
  if (!validation.ok) {
    showMessage(validation.message)
    return
  }

  isSaving.value = true
  try {
    await http.post('/gopico/update/authorization', {
      auth_id: selectedData.value.auth_id,
      days: validation.value.days,
      max_devices: validation.value.max_streams,
      role: validation.value.role,
    })
    await queryAuthorizations(currentPage.value, pageSize.value)
    showMessage('修改成功')
    dialogVisible.value = false
  } catch (err: any) {
    showError(err, '修改失败')
  } finally {
    isSaving.value = false
  }
}

const handleOpenCreate = () => {
  createForm.value = {
    name: '',
    machine_code: '',
    role: '',
    days: '',
    max_devices: '',
  }
  createDialogVisible.value = true
}

const handleCreate = async () => {
  if (isCreating.value) return
  const validation = validateCreateAuthorization({
    name: createForm.value.name,
    machine_code: createForm.value.machine_code,
    role: createForm.value.role,
    days: createForm.value.days,
    max_streams: createForm.value.max_devices,
    product: 'gopico',
  })
  if (!validation.ok) {
    showMessage(validation.message)
    return
  }

  createForm.value.name = validation.value.name
  createForm.value.machine_code = validation.value.machine_code

  isCreating.value = true
  try {
    const res = await http.post('/gopico/create/new/deploy/authorization', {
      name: validation.value.name,
      machine_code: validation.value.machine_code,
      days: validation.value.days,
      max_devices: validation.value.max_streams,
      role: validation.value.role,
    })
    deployInfo.value = res.data.data || ''

    createDialogVisible.value = false
    deployDialogVisible.value = true
    await queryAuthorizations(currentPage.value, pageSize.value)
    showMessage('创建成功')
  } catch (err: any) {
    showError(err, '创建失败')
  } finally {
    isCreating.value = false
  }
}

const fetchDeployInfo = async (row: GopicoAuthorization) => {
  const params = {
    auth_id: row.auth_id,
  }
  const response = await http.get('/gopico/query/deploy/authorization/by/id', {params})
  return response.data.data || ''
}

const handleCopyDeploy = async (row: GopicoAuthorization) => {
  let deployStr = ''
  try {
    deployStr = await fetchDeployInfo(row)
  } catch (err: any) {
    showError(err, '获取 Deploy 信息失败')
    return
  }
  try {
    await navigator.clipboard.writeText(deployStr)
    ElMessage.success('已复制到剪贴板')
  } catch {
    ElMessage.error('复制失败')
  }
}

const downloadDeployFile = (deployStr: string, name: string) => {
  const blob = new Blob([deployStr], { type: 'text/plain;charset=utf-8' })

  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `gopico-auth-${name}.info`
  a.click()

  URL.revokeObjectURL(url)
}

const handleDownloadDeploy = async (row: GopicoAuthorization) => {
  let deployStr = ''
  try {
    deployStr = await fetchDeployInfo(row)
  } catch (err: any) {
    showError(err, '获取 Deploy 信息失败')
    return
  }
  downloadDeployFile(deployStr, row.auth_name)
}

const copyDeployInfo = async () => {
  try {
    await navigator.clipboard.writeText(deployInfo.value)
    ElMessage.success('已复制到剪贴板')
  } catch {
    ElMessage.error('复制失败')
  }
}

const downloadDeployInfo = () => {
  downloadDeployFile(deployInfo.value, createForm.value.name)
}

onMounted(async () => {
  await queryAuthorizations(currentPage.value, pageSize.value)
})


const pageSize = ref(20)
const currentPage = ref(1)
const size = ref<ComponentSize>('default')
const background = ref(false)
const disabled = ref(false)

const handleSizeChange = async (val: number) => {
  pageSize.value = val
  await queryAuthorizations(currentPage.value, pageSize.value)
}

const handleCurrentChange = async (val: number) => {
  currentPage.value = val
  await queryAuthorizations(currentPage.value, pageSize.value);
}

</script>

<style scoped>

</style>
