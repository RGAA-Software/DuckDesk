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

  <el-table :data="tableData" stripe style="width: 100%">
    <el-table-column label="Auth ID" prop="auth_id" />
    <el-table-column label="名称" prop="auth_name" :min-width="50"/>
    <el-table-column label="Product" prop="product" :min-width="70">
      <template #default="scope">
        {{ scope.row.product || 'cms' }}
      </template>
    </el-table-column>
    <el-table-column label="Machine Code" prop="machine_code" :min-width="120" />
    <el-table-column label="创建时间" prop="created_timestamp_ms" :formatter="formatDateCreateTime"/>
    <el-table-column label="结束时间" prop="end_timestamp_ms" :formatter="formatDateEndTime"/>
    <el-table-column label="授权时间(天)" prop="days" />
    <el-table-column label="剩余时间(天)" prop="left_days" />
    <el-table-column label="状态" :min-width="70">
      <template #default="scope">
        <el-tag v-if="scope.row.revoked" type="danger" size="small">已吊销</el-tag>
        <el-tag v-else type="success" size="small">有效</el-tag>
      </template>
    </el-table-column>
    <el-table-column align="left" :min-width="220">
      <template #header>
        <el-input v-model="search" size="default" placeholder="搜索" @keyup.enter="handleSearch"  />
      </template>
      <template #default="scope">
        <el-button size="small" @click="handleShowInfo(scope.$index, scope.row)">
          显示
        </el-button>

        <el-button size="small" @click="handleModifyInfo(scope.row)">
          修改
        </el-button>
        <el-button
          v-if="(scope.row.product || 'cms') === 'gopico' && !scope.row.revoked"
          size="small"
          type="danger"
          @click="handleRevoke(scope.row)"
        >
          吊销
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

  <!-- 弹出对话框 -->
  <el-dialog v-model="dialogVisible" title="详细信息" width="60%">
    <div v-if="selectedData">
<!--      <p><strong>Auth ID:</strong> {{ selectedData.auth_id }}</p>-->
<!--      <p><strong>名称:</strong> {{ selectedData.auth_name }}</p>-->
<!--      <p><strong>Machine Code:</strong> {{ selectedData.machine_code }}</p>-->
<!--      <p><strong>Appkey:</strong> {{ selectedData.appkey }}</p>-->
<!--      <p><strong>App Secret:</strong> {{ selectedData.app_secret }}</p>-->
<!--      <p><strong>用户名:</strong> {{ selectedData.username }}</p>-->
<!--      <p><strong>Days:</strong> {{ selectedData.days }}</p>-->
<!--      <p><strong>Verify Server:</strong> {{ selectedData.verify_server }}</p>-->

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

        <!-- Appkey -->
        <el-form-item label="Appkey">
          <el-input v-model="selectedData.appkey" disabled></el-input>
        </el-form-item>

        <!-- Verify Server -->
        <el-form-item label="Verify Server">
          <el-input v-model="selectedData.verify_server" disabled></el-input>
        </el-form-item>

        <!-- Days (下拉框选择) -->
        <el-form-item label="Days">
          <el-select v-model="selectedData.days" placeholder="请选择" :disabled="selectedData.disable_modify">
            <el-option label="7" value="7"></el-option>
            <el-option label="30" value="30"></el-option>
            <el-option label="365000" value="365000"></el-option>
          </el-select>
        </el-form-item>

        <el-form-item label="Product">
          <el-input :model-value="selectedData.product || 'cms'" disabled></el-input>
        </el-form-item>

        <el-form-item :label="(selectedData.product || 'cms') === 'gopico' ? 'Max Devices' : 'Max Streams'">
          <el-input v-model="selectedData.max_streams" :disabled="selectedData.disable_modify"></el-input>
        </el-form-item>

        <!-- Authorization Information -->
        <el-form-item label="Deploy Information">
          <div class="deploy-wrapper">
            <el-input
                v-model="selectedData.deploy_str"
                type="textarea"
                :rows="4"
                readonly
            />

            <div class="deploy-actions">
              <el-button size="small" @click="copyDeployInfo">复制</el-button>
              <el-button size="small" @click="saveDeployInfo">下载</el-button>
            </div>
          </div>
        </el-form-item>

      </el-form>

    </div>
    <template #footer>
      <el-button @click="dialogVisible = false">关闭</el-button>
      <el-button
        v-if="saveBtnVisible"
        type="primary"
        :loading="isSaving"
        :disabled="isSaving"
        @click="handleSave"
      >
        保存
      </el-button>
    </template>
  </el-dialog>

</template>

<script lang="ts" setup>
import {onMounted, ref} from 'vue'
import { watch } from 'vue'
import {type ComponentSize, ElMessage} from 'element-plus'
import http from '@/utils/http'
import { validateUpdateAuthorization } from '@/utils/authorizationValidation'

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

interface Authorization {
  auth_id: string
  auth_name: string
  machine_code: string
  description: string
  max_streams: number
  appkey: string
  app_secret: string
  username: string
  password: string
  created_timestamp_ms: number
  end_timestamp_ms: number
  days: number
  verify_server: string
  deploy_str: string
  role: number
  product?: string
  revoked?: boolean
  total: number
  //
  disable_modify: boolean
  left_days: number
}

const search = ref('')
const tableData = ref<Authorization[]>([])
const selectedData = ref<Authorization | null>(null)
const dialogVisible = ref(false)
const saveBtnVisible = ref(false)
const totalAuthCount = ref(0)
const isSaving = ref(false)
const isRevoking = ref(false)

const updateTableData = (items: Authorization[]) => {
  tableData.value = items
  totalAuthCount.value = items.length > 0 ? Number(items[0].total) : 0
  tableData.value.forEach((item) => {
    item.left_days = Math.floor(item.days - (Date.now() - item.created_timestamp_ms)/24/3600/1000)
  })
}

const showError = (err: any, fallback: string) => {
  errorMessage.value = err.response?.data?.message || fallback
  errorDialogVisible.value = true
}

const showMessage = (message: string) => {
  errorMessage.value = message
  errorDialogVisible.value = true
}

const validateSelectedAuthorization = () => {
  if (!selectedData.value) return false

  const validation = validateUpdateAuthorization(selectedData.value)
  if (!validation.ok) {
    showMessage(validation.message)
    return false
  }

  selectedData.value.days = validation.value.days
  selectedData.value.max_streams = validation.value.max_streams
  return true
}

const formatDateCreateTime = (obj: Authorization) => {
  const date = new Date(obj.created_timestamp_ms);
  return date.toLocaleString();
}

const formatDateEndTime = (obj: Authorization) => {
  const date = new Date(obj.end_timestamp_ms);
  return date.toLocaleString();
}

// GET 请求函数
const queryAuthorizations = async (page: number, pageSize: number) => {
  // loading.value = true
  // error.value = null
  try {
    const params = {
      page: page,
      page_size: pageSize,
    }
    const response = await http.get('/query/authorizations', {params})
    updateTableData(response.data.data || [])
  } catch (err: any) {
    tableData.value = []
    totalAuthCount.value = 0
    showError(err, '请求授权列表失败')
  }
}

const searchAuthorization = async () => {
  try {
    const params = {
      page: 1,
      page_size: 10,
      auth_name: search.value,
    }
    const response = await http.get('/query/authorization/like/name', {params})
    updateTableData(response.data.data || [])
  } catch (err: any) {
    tableData.value = []
    totalAuthCount.value = 0
    showError(err, '搜索授权失败')
  }
}

// const filterTableData = computed(() =>
//   // tableData.filter(
//   //   (data) =>
//   //     !search.value ||
//   //     data.name.toLowerCase().includes(search.value.toLowerCase())
//   // )
//   tableData
// )

const handleShowInfo = (index: number, row: Authorization) => {
  selectedData.value = row
  selectedData.value.disable_modify = true
  dialogVisible.value = true
  saveBtnVisible.value = false
}

const handleModifyInfo = (row: Authorization) => {
  selectedData.value = row
  selectedData.value.disable_modify = false
  dialogVisible.value = true
  saveBtnVisible.value = true
}

const handleRevoke = async (row: Authorization) => {
  if (isRevoking.value) return
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
  if (!validateSelectedAuthorization()) return

  const isGopico = (selectedData.value.product || 'cms') === 'gopico'
  const payload = isGopico
    ? {
        auth_id: selectedData.value.auth_id,
        days: Number(selectedData.value.days),
        max_devices: Number(selectedData.value.max_streams),
      }
    : {
        auth_id: selectedData.value.auth_id,
        days: Number(selectedData.value.days),
        max_streams: Number(selectedData.value.max_streams),
      }

  isSaving.value = true
  try {
    await http.post(
      isGopico ? '/gopico/update/authorization' : '/update/authorization',
      payload,
    )
    await queryAuthorizations(currentPage.value, pageSize.value)
    showMessage('修改成功')
    dialogVisible.value = false
  } catch (err: any) {
    showError(err, '修改失败')
  } finally {
    isSaving.value = false
  }
}

const copyDeployInfo = async () => {
  try {
    if (!selectedData.value) return
    const content = selectedData.value.deploy_str
    await navigator.clipboard.writeText(content)
    ElMessage.success('已复制到剪贴板')
  } catch (e) {
    ElMessage.error('复制失败')
  }
}

const saveDeployInfo = () => {
  if (!selectedData.value) return
  const content = selectedData.value.deploy_str
  const auth_name = selectedData.value.auth_name
  const blob = new Blob([content], { type: 'text/plain;charset=utf-8' })

  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `auth-${auth_name}.info`
  a.click()

  URL.revokeObjectURL(url)
}

const handleSearch = () => {
  if (!search.value) {
    void queryAuthorizations(currentPage.value, pageSize.value)
    return
  }
  void searchAuthorization()
}

watch(search, async (newVal) => {
  if (!newVal) {
    await queryAuthorizations(currentPage.value, pageSize.value) // 立刻拉全量
  }
})

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

.deploy-wrapper {
  position: relative;
  width: 100%;
}

.deploy-actions {
  position: absolute;
  right: -150px;              /* 给横向按钮留空间 */
  top: 50%;
  transform: translateY(-50%);
  display: flex;
  flex-direction: row;        /* 横向排列 */
  gap: 8px;
}

.deploy-actions .el-button {
  width: 64px;
  height: 28px;
  padding: 0;
}


</style>
