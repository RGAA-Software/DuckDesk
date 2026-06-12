<script setup lang="ts">
import iconLogo from '@/assets/ic_logo.png'
import { computed, onMounted, ref } from 'vue'
import { ElNotification, type UploadFile } from 'element-plus'
import { queryAuthorization, updateAuthorization } from '@/model/auth_api.ts'
import type { Authorization } from '@/entity/authorization.ts'
import axiosHttp, { BASE_URL } from '@/http.ts'

import CryptoJS from 'crypto-js'

const md5 = (input: string): string => {
  return CryptoJS.MD5(input).toString()
}

import { useRouter } from 'vue-router'
import { copyText } from '@/util/clipboard.ts'
import { downloadTxt } from '@/util/download.ts'
import { queryMachineCode } from '@/model/spvr_api.ts'
const router = useRouter()

const inputUsername = ref('')
const inputPassword = ref('')
const authorization = ref<Authorization>()
const uploadAuthDialog = ref(false)
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

  // query authorization
  authorization.value = await queryAuthorization()
  if (authorization.value) {
    console.log('update authorization appkey', authorization.value.appkey)
    localStorage.setItem('appkey', authorization.value.appkey)
  }
  console.log('auth: `', authorization?.value)
})

const isDefaultAuth = computed(() => {
  if (authorization.value) {
    return (
      authorization.value.machine_code === 'MC-001' || authorization.value.machine_code === 'MC-002'
    )
  }
  return false
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
    const resp = await axiosHttp.post(
      '/api/v1/auth/control/verify/auth/account?appkey=' + localStorage.getItem('appkey'),
      {
        username: username,
        password: md5(password),
      },
    )
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

const handleSelectAuthFile = async (file: UploadFile) => {
  console.log(file)
  if (!file.raw) {
    console.error('file not found', file)
    return
  }

  const reader = new FileReader()
  reader.readAsText(file.raw, 'utf-8')

  reader.onload = async () => {
    const content = reader.result as string
    console.log('文件内容:', content)
    authorization.value = await updateAuthorization(content)
    // update ui
    if (authorization.value) {
      inputUsername.value = authorization.value.username
      inputPassword.value = authorization.value.password

      localStorage.setItem('appkey', authorization.value.appkey)

      uploadAuthDialog.value = true
    }
  }
}

async function handleCopyAuthInfo() {
  const info = `地址: ${BASE_URL}\n账号: ${authorization.value?.username}\n密码: ${authorization.value?.password}`
  await copyText(info)
  ElNotification({
    message: '保存成功',
    type: 'success',
  })
}

async function handleDownloadInfo() {
  const info = `地址: ${BASE_URL}\n账号: ${authorization.value?.username}\n密码: ${authorization.value?.password}`
  await downloadTxt('GoDesk_Account.txt', info)
  ElNotification({
    message: '下载成功',
    type: 'success',
  })
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

// handle auto fill in
async function handleAutoFillIn() {
  if (authorization.value) {
    inputUsername.value = authorization.value.username
    inputPassword.value = authorization.value.password
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

    <div>
      <div class="flex justify-center items-center">
        <span class="text-slate-700 font-semibold">or</span>
      </div>
    </div>
  </div>

  <div>
    <div class="h-1" />
    <div>
      <div class="flex justify-center items-center">
        <el-upload :auto-upload="false" :on-change="handleSelectAuthFile" :limit="1">
          <el-button class="!text-small !font-semibold w-40" type="primary" link>
            上传授权
          </el-button>
        </el-upload>
      </div>
    </div>

    <div class="h-2" />
    <div class="flex justify-center items-center">
      <div class="w-100 flex justify-start items-center">
        <span class="">机器码:</span>
        <span class="ml-1 bg-blue-200">{{ machineCode }}</span>
      </div>
      <el-button class="ml-2 w-22" type="primary" plain @click="handleCopyMachineCode"
        >复制</el-button
      >
    </div>

    <div class="h-5" />
    <div v-if="isDefaultAuth && authorization">
      <div class="flex justify-center items-center">
        <div class="w-100 flex justify-start items-center">
          <span>当前为免费授权</span>
          <span class="pl-2">账号:</span>
          <span class="ml-1 bg-blue-200">{{ authorization?.username }}</span>
          <span class="pl-2">密码:</span>
          <span class="ml-1 bg-blue-200">{{ authorization?.password }}</span>
        </div>
        <el-button class="ml-2 w-22" type="primary" plain @click="handleAutoFillIn"
          >自动填入</el-button
        >
      </div>
    </div>
  </div>

  <el-dialog
    v-model="uploadAuthDialog"
    title="账号密码信息"
    :modal="false"
    modal-penetrable
    center
    destroy-on-close
    class="!w-100 font-bold"
  >
    <div class="">
      <div class="h-2" />

      <div class="flex justify-center">
        <span class="w-12">账号</span>
        <span class="w-22">{{ authorization?.username }}</span>
      </div>

      <div class="h-2" />

      <div class="flex justify-center">
        <span class="w-12">密码</span>
        <span class="w-22">{{ authorization?.password }}</span>
      </div>

      <div class="h-2" />
      <div class="flex justify-center">
        <span class="text-amber-600 !font-normal"
          >请复制保存此账号密码信息，若是遗忘，重新上传授权即可</span
        >
      </div>
    </div>

    <template #footer>
      <div class="dialog-footer">
        <el-button type="primary" @click="handleCopyAuthInfo"> 复制信息 </el-button>
        <el-button type="warning" @click="handleDownloadInfo"> 下载信息 </el-button>
        <el-button type="danger" @click="uploadAuthDialog = false"> 关闭 </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<style scoped></style>
