<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { message } from 'ant-design-vue'
import type { ApplicationCard } from './api'
import { getPublicApps, openGuestInstance, startPublicApp, waitForGuestInstance } from './public_api'

const apps = ref<ApplicationCard[]>([])
const busy = ref('')
async function refresh() {
  try { apps.value = await getPublicApps() }
  catch { message.error('公共应用加载失败') }
}
async function enter(app: ApplicationCard) {
  busy.value = app.app_id
  try {
    const started = await startPublicApp(app.app_id)
    const instance = started.instance.reconnectable
      ? started.instance
      : await waitForGuestInstance(started.instance.instance_id)
    await openGuestInstance(instance, started.clientNonce)
  } catch (error: any) {
    message.error(error?.response?.data?.message || error?.message || '应用启动失败')
  } finally { busy.value = '' }
}
onMounted(refresh)
</script>

<template>
  <main class="min-h-screen bg-slate-950 px-6 py-10">
    <div class="mx-auto max-w-6xl">
      <header class="mb-8 flex items-center justify-between text-white">
        <div><h1 class="text-2xl font-semibold">公共云端应用</h1><p class="mt-2 text-slate-400">无需账号；启动和连接仍受一次性票据与访客配额保护。</p></div>
        <RouterLink to="/user/login"><a-button>用户登录</a-button></RouterLink>
      </header>
      <a-empty v-if="apps.length === 0" description="暂无公开应用" />
      <a-row v-else :gutter="[18,18]">
        <a-col v-for="app in apps" :key="app.app_id" :xs="24" :md="12" :xl="8">
          <a-card hoverable><a-card-meta :title="app.name" description="公开应用" />
            <div class="mt-5 flex justify-end"><a-button type="primary" :loading="busy === app.app_id" @click="enter(app)">启动并进入</a-button></div>
          </a-card>
        </a-col>
      </a-row>
    </div>
  </main>
</template>
