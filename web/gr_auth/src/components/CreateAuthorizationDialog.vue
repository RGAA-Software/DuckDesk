<script setup lang="ts">
import { computed, ref } from 'vue'
import { ElMessage } from 'element-plus'
import http from '@/utils/http'

import { useAuthStore } from '@/stores/auth'

const authStore = useAuthStore()

const props = defineProps<{
  modelValue: boolean
}>()

const emit = defineEmits(['update:modelValue'])

const visible = computed({
  get: () => props.modelValue,
  set: (val) => emit('update:modelValue', val)
})

const errorDialogVisible = ref(false)
const deployDialogVisible = ref(false)
const deployInfo = ref('')

// 错误信息
const errorMessage = ref('')

const handleCreate = async ()  => {
  if (!form.value.name || !form.value.machine_code || !form.value.role || !form.value.days || !form.value.max_streams) {
    errorMessage.value = '请填写完整授权信息'
    errorDialogVisible.value = true
    return
  }

  // 1. 组装后端需要的数据
  const payload = {
    name: form.value.name,
    machine_code: form.value.machine_code,
    role: Number(form.value.role),
    days: Number(form.value.days),
    max_streams: Number(form.value.max_streams)
  }

  try {
    // 2. 调用后台接口
    const res = await http.post('/create/new/deploy/authorization', payload)
    deployInfo.value = res.data.data || ''

    authStore.triggerRefresh()
    visible.value = false
    deployDialogVisible.value = true
    // 3. 成功提示
    errorMessage.value = '创建成功'
  } catch (err: any) {
    errorMessage.value = err.response?.data?.message || '创建失败'
    errorDialogVisible.value = true
  }
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
  const blob = new Blob([deployInfo.value], { type: 'text/plain;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `auth-${form.value.name}.info`
  a.click()
  URL.revokeObjectURL(url)
}

const form = ref({
  name: '',
  machine_code: '',
  role: '',
  days: '',
  max_streams: ''
})
</script>

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

  <!-- 弹出对话框 -->
  <el-dialog v-model="visible" title="创建授权信息" width="30%" :close-on-click-modal="false">
    <el-form label-width="auto" style="max-width: 800px">

      <!-- 用户名 -->
      <el-form-item label="User Name">
        <el-input v-model="form.name"></el-input>
      </el-form-item>

      <!-- Machine Code -->
      <el-form-item label="Machine Code">
        <el-input v-model="form.machine_code"></el-input>
      </el-form-item>

      <!-- 用户角色 -->
      <el-form-item label="Customer Role">
        <el-select  v-model="form.role" placeholder="请选择">
          <el-option label="1" value="1"></el-option>
          <el-option label="2" value="2"></el-option>
          <el-option label="3" value="3"></el-option>
        </el-select>
      </el-form-item>

      <!-- Days (下拉框选择) -->
      <el-form-item label="Days">
        <el-select v-model="form.days" placeholder="请选择">
          <el-option label="7" value="7"></el-option>
          <el-option label="30" value="30"></el-option>
          <el-option label="365" value="365"></el-option>
        </el-select>
      </el-form-item>

      <!-- max streams -->
      <el-form-item label="Max Streams">
        <el-input v-model="form.max_streams"></el-input>
      </el-form-item>
    </el-form>
    <template #footer>
      <el-button type="primary" @click="handleCreate">创建</el-button>
    </template>
  </el-dialog>
</template>

<style scoped>

</style>
