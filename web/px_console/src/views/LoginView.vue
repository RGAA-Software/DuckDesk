<script setup lang="ts">
import iconLogo from '@/assets/ic_logo.png'
import { computed, onMounted, ref } from 'vue'
import { notification } from 'ant-design-vue'
import { pullAuthorization, queryAuthStatus, type AuthStatus } from '@/model/auth_api.ts'
import { BASE_URL } from '@/http.ts'
import { loginAdmin } from '@/model/admin_session_api.ts'

import { useRouter } from 'vue-router'
import { copyText } from '@/util/clipboard.ts'
import { formatTimestamp } from '@/util/time.ts'
import { queryMachineCode } from '@/model/console_api.ts'
const router = useRouter()

const inputUsername = ref('')
const inputPassword = ref('')
const authStatus = ref<AuthStatus | null>(null)
const refreshing = ref(false)
const machineCode = ref('')

onMounted(async () => {
  console.log('url: ', BASE_URL)

  // username
  const savedUsername = localStorage.getItem('username')
  if (savedUsername) {
    inputUsername.value = savedUsername
  }

  // query machine code
  machineCode.value = await queryMachineCode()

  // query authorization status
  await refreshAuthStatus()
})

async function refreshAuthStatus() {
  authStatus.value = await queryAuthStatus()
  console.log('auth status:', authStatus.value)
}

async function handleRefreshAuth() {
  if (refreshing.value) return
  refreshing.value = true
  try {
    await pullAuthorization()
    await refreshAuthStatus()
  } finally {
    refreshing.value = false
  }
}

// 授权状态展示
const authStatusText = computed(() => {
  const st = authStatus.value
  if (!st || !st.authorized) {
    return '未授权'
  }
  if (st.mode === 'trial') {
    return '试用中'
  }
  if (!st.valid) {
    return '已过期'
  }
  return '已授权'
})

const authStatusType = computed(() => {
  switch (authStatusText.value) {
    case '已授权':
      return 'success'
    case '试用中':
      return 'warning'
    default:
      return 'error'
  }
})

const authExpireText = computed(() => {
  const st = authStatus.value
  if (!st || !st.authorized) {
    return ''
  }
  if (st.mode === 'trial') {
    return '不限时间'
  }
  if (!st.end_timestamp_ms) {
    return ''
  }
  const leftDays = Math.max(0, Math.ceil((st.end_timestamp_ms - Date.now()) / 24 / 3600 / 1000))
  return `到期时间: ${formatTimestamp(st.end_timestamp_ms)} (剩余 ${leftDays} 天)`
})

async function handleLogin() {
  localStorage.setItem('username', inputUsername.value)
  if (await login(inputUsername.value, inputPassword.value)) {
    // replace this page and jump to main page
    await router.replace('/home')
  }
}

async function login(username: string, password: string) {
  try {
    const profile = await loginAdmin(username, password)
    if (!profile) {
      notification.error({
        message: '登录失败：账号或密码错误',
      })
      return false
    } else {
      notification.success({
        message: '登录成功',
      })

      //
      return true
    }
  } catch (e) {
    console.error(e)
    notification.error({
      message: '登录失败，经检查输入信息和网络',
    })
    return false
  }
}

// copy machine code
async function handleCopyMachineCode() {
  if (machineCode.value) {
    await copyText(machineCode.value)
    notification.success({
      message: '机器码复制成功',
    })
  }
}

</script>

<template>
  <div>
    <div class="h-45"></div>

    <div class="flex justify-center items-center h-12">
      <a-image :src="iconLogo" class="w-38" :preview="false" />
      <span class="h-12 text-2xl text-slate-700 font-bold flex items-center">登录</span>
    </div>
  </div>

  <div>
    <div class="h-4" />

    <div>
      <div class="flex justify-center items-center">
        <span class="text-lg text-slate-700 w-16">用户名</span>
        <span class="w-4" />
        <a-input v-model:value="inputUsername" placeholder="请输入" class="!w-60" />
      </div>
    </div>

    <div class="h-4" />

    <div>
      <div class="flex justify-center items-center">
        <span class="text-lg text-slate-700 w-16">密码</span>
        <span class="w-4" />
        <a-input-password v-model:value="inputPassword" placeholder="请输入密码" class="!w-60" />
      </div>
    </div>

    <div class="h-4" />

    <div>
      <div class="flex justify-center items-center">
        <span class="text-lg text-slate-700 w-16"></span>
        <span class="w-4" />
        <a-button class="!w-60" type="primary" @click="handleLogin">登录</a-button>
      </div>
    </div>

    <div class="h-4" />
  </div>

  <div>
    <div class="h-1" />

    <div class="flex justify-center items-center">
      <div class="w-100 flex justify-start items-center">
        <span class="">机器码:</span>
        <span class="ml-1 bg-blue-200">{{ machineCode }}</span>
      </div>
      <a-button class="ml-2 w-22" type="primary" @click="handleCopyMachineCode">复制</a-button>
    </div>

    <div class="h-2" />

    <div class="flex justify-center items-center">
      <div class="w-100 flex justify-start items-center">
        <span class="">授权状态:</span>
        <a-tag class="ml-1" :color="authStatusType" size="small">{{ authStatusText }}</a-tag>
        <span v-if="authExpireText" class="ml-2 text-slate-500 text-sm">{{ authExpireText }}</span>
      </div>
      <a-button class="ml-2 w-22" type="primary" :loading="refreshing" @click="handleRefreshAuth"
        >刷新状态</a-button
      >
    </div>

    <div class="h-5" />
  </div>
</template>

<style scoped></style>
