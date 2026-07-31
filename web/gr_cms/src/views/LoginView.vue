<script setup lang="ts">
import iconLogo from '@/assets/ic_logo.png'
import { computed, onMounted, ref } from 'vue'
import { ElNotification } from 'element-plus'
import { pullAuthorization, queryAuthStatus, type AuthStatus } from '@/model/auth_api.ts'
import axiosHttp, { BASE_URL } from '@/http.ts'

import CryptoJS from 'crypto-js'

const md5 = (input: string): string => {
  return CryptoJS.MD5(input).toString()
}

import { useRouter } from 'vue-router'
import { copyText } from '@/util/clipboard.ts'
import { formatTimestamp } from '@/util/time.ts'
import { queryMachineCode } from '@/model/spvr_api.ts'
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
  autofillLogin(authStatus.value)
}

// 本机访问时后端会附带登录凭据，自动填入登录表单
function autofillLogin(st: AuthStatus | null) {
  if (st?.username) {
    inputUsername.value = st.username
  }
  if (st?.password) {
    inputPassword.value = st.password
  }
}

async function handleRefreshAuth() {
  if (refreshing.value) return
  refreshing.value = true
  try {
    const st = await pullAuthorization()
    autofillLogin(st)
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
      return 'danger'
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
    localStorage.setItem('token', inputPassword.value)
    // replace this page and jump to main page
    await router.replace('/home')
  }
}

async function login(username: string, password: string) {
  try {
    const resp = await axiosHttp.post('/api/v1/auth/control/verify/auth/account', {
      username: username,
      password: md5(password),
    })
    if (resp.status !== 200) {
      console.error('change password failed', resp)
      return false
    }

    const data = resp.data
    if (data.code !== 200) {
      console.error('login failed, data:', data)
      ElNotification({
        message: '登录失败:' + data.code,
        type: 'error',
      })
      return false
    } else {
      // 登录成功后端返回 appkey，保存供后续受 appkey filter 保护的接口使用
      if (data.data) {
        localStorage.setItem('appkey', data.data)
      }
      ElNotification({
        message: '登录成功',
        type: 'success',
      })

      //
      return true
    }
  } catch (e) {
    console.error(e)
    ElNotification({
      message: '登录失败，经检查输入信息和网络',
      type: 'error',
    })
    return false
  }
}

// copy machine code
async function handleCopyMachineCode() {
  if (machineCode.value) {
    await copyText(machineCode.value)
    ElNotification({
      message: '机器码复制成功',
      type: 'success',
    })
  }
}

</script>

<template>
  <div>
    <div class="h-45"></div>

    <div class="flex justify-center items-center h-12">
      <el-image :src="iconLogo" class="w-38" />
      <span class="h-12 text-2xl text-slate-700 font-bold flex items-center">登录</span>
    </div>
  </div>

  <div>
    <div class="h-4" />

    <div>
      <div class="flex justify-center items-center">
        <span class="text-lg text-slate-700 w-16">用户名</span>
        <span class="w-4" />
        <el-input v-model="inputUsername" placeholder="请输入" class="!w-60"></el-input>
      </div>
    </div>

    <div class="h-4" />

    <div>
      <div class="flex justify-center items-center">
        <span class="text-lg text-slate-700 w-16">密码</span>
        <span class="w-4" />
        <el-input
          v-model="inputPassword"
          type="password"
          placeholder="请输入密码"
          show-password
          class="!w-60"
        ></el-input>
      </div>
    </div>

    <div class="h-4" />

    <div>
      <div class="flex justify-center items-center">
        <span class="text-lg text-slate-700 w-16"></span>
        <span class="w-4" />
        <el-button class="!w-60" type="primary" plain @click="handleLogin">登录</el-button>
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
      <el-button class="ml-2 w-22" type="primary" plain @click="handleCopyMachineCode"
        >复制</el-button
      >
    </div>

    <div class="h-2" />

    <div class="flex justify-center items-center">
      <div class="w-100 flex justify-start items-center">
        <span class="">授权状态:</span>
        <el-tag class="ml-1" :type="authStatusType" size="small">{{ authStatusText }}</el-tag>
        <span v-if="authExpireText" class="ml-2 text-slate-500 text-sm">{{ authExpireText }}</span>
      </div>
      <el-button
        class="ml-2 w-22"
        type="primary"
        plain
        :loading="refreshing"
        @click="handleRefreshAuth"
        >刷新状态</el-button
      >
    </div>

    <div class="h-5" />
  </div>
</template>

<style scoped></style>
