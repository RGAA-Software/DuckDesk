<script setup lang="ts">
import { ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { message } from 'ant-design-vue'
import { loginUser } from './api'
import { registerUser } from './public_api'

const username = ref('')
const password = ref('')
const loading = ref(false)
const registering = ref(false)
const registerOpen = ref(false)
const registerName = ref('')
const registerPassword = ref('')
const router = useRouter()
const route = useRoute()

async function submit() {
  if (loading.value || !username.value || !password.value) return
  loading.value = true
  try {
    await loginUser(username.value, password.value)
    const target = String(route.query.redirect || '/user/home')
    await router.replace(target.startsWith('/user/') ? target : '/user/home')
  } catch {
    message.error('用户名或密码错误')
  } finally {
    loading.value = false
  }
}

async function submitRegister() {
  if (!registerName.value || !registerPassword.value) return
  registering.value = true
  try {
    await registerUser(registerName.value, registerPassword.value)
    username.value = registerName.value
    password.value = registerPassword.value
    registerOpen.value = false
    message.success('注册成功，请登录')
  } catch (error: any) {
    message.error(error?.response?.data?.message || '注册失败，请检查用户名和密码')
  } finally {
    registering.value = false
  }
}

</script>

<template>
  <main class="flex min-h-screen items-center justify-center bg-slate-950 px-4">
    <a-card class="w-full max-w-md" title="登录 Pixels 用户门户">
      <a-form layout="vertical" @finish="submit">
        <a-form-item label="用户名"><a-input v-model:value="username" autocomplete="username" /></a-form-item>
        <a-form-item label="密码"><a-input-password v-model:value="password" autocomplete="current-password" /></a-form-item>
        <a-button type="primary" html-type="submit" block :loading="loading" @click="submit">登录</a-button>
      </a-form>
      <div class="mt-4 flex justify-between"><RouterLink to="/user/public-apps">游客使用公开应用</RouterLink><a-button type="link" size="small" @click="registerOpen = true">注册账号</a-button><RouterLink to="/">返回管理后台</RouterLink></div>
    </a-card>
    <a-modal v-model:open="registerOpen" title="注册 Pixels 用户" :confirm-loading="registering" @ok="submitRegister">
      <a-form layout="vertical">
        <a-form-item label="用户名"><a-input v-model:value="registerName" autocomplete="username" /></a-form-item>
        <a-form-item label="密码（8–128 位）"><a-input-password v-model:value="registerPassword" autocomplete="new-password" /></a-form-item>
      </a-form>
    </a-modal>
  </main>
</template>
