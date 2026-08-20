<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { message } from 'ant-design-vue'
import { loginUser } from './api'
import { getRegistrationPolicy, registerUser } from './public_api'

const username = ref('')
const password = ref('')
const loading = ref(false)
const registerMode = ref<'closed' | 'invite' | 'open'>('closed')
const registering = ref(false)
const registerOpen = ref(false)
const registerName = ref('')
const registerPassword = ref('')
const inviteCode = ref('')
const router = useRouter()
const route = useRoute()

async function submit() {
  if (loading.value || !username.value || !password.value) return
  loading.value = true
  try {
    const profile = await loginUser(username.value, password.value)
    const target = profile.must_change_password ? '/user/profile' : String(route.query.redirect || '/user/home')
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
    await registerUser(registerName.value, registerPassword.value, inviteCode.value)
    username.value = registerName.value
    password.value = registerPassword.value
    registerOpen.value = false
    message.success('注册成功，请登录')
  } catch (error: any) {
    message.error(error?.response?.status === 403 ? '当前不允许自主注册' : '注册失败，请检查用户名、密码或邀请码')
  } finally {
    registering.value = false
  }
}

onMounted(async () => {
  try { registerMode.value = (await getRegistrationPolicy()).mode } catch { /* login remains available */ }
})
</script>

<template>
  <main class="flex min-h-screen items-center justify-center bg-slate-950 px-4">
    <a-card class="w-full max-w-md" title="登录 Pixels 用户门户">
      <a-form layout="vertical" @finish="submit">
        <a-form-item label="用户名"><a-input v-model:value="username" autocomplete="username" /></a-form-item>
        <a-form-item label="密码"><a-input-password v-model:value="password" autocomplete="current-password" /></a-form-item>
        <a-button type="primary" html-type="submit" block :loading="loading" @click="submit">登录</a-button>
      </a-form>
      <div class="mt-4 flex justify-between"><RouterLink to="/user/public-apps">游客使用公开应用</RouterLink><a-button v-if="registerMode !== 'closed'" type="link" size="small" @click="registerOpen = true">注册账号</a-button><RouterLink to="/">返回管理后台</RouterLink></div>
    </a-card>
    <a-modal v-model:open="registerOpen" title="注册 Pixels 用户" :confirm-loading="registering" @ok="submitRegister">
      <a-form layout="vertical">
        <a-form-item label="用户名"><a-input v-model:value="registerName" autocomplete="username" /></a-form-item>
        <a-form-item label="密码（8–128 位）"><a-input-password v-model:value="registerPassword" autocomplete="new-password" /></a-form-item>
        <a-form-item v-if="registerMode === 'invite'" label="邀请码"><a-input v-model:value="inviteCode" autocomplete="one-time-code" /></a-form-item>
      </a-form>
    </a-modal>
  </main>
</template>
