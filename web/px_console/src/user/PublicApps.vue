<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { message, Modal } from 'ant-design-vue'
import type { ApplicationCard, InstanceView } from './api'
import { getGuestInstances, getPublicApps, openGuestInstance, startPublicApp, stopGuestInstance, waitForGuestInstance } from './public_api'

const apps = ref<ApplicationCard[]>([])
const instances = ref<InstanceView[]>([])
const busy = ref('')
let timer = 0
const activeByApp = computed(() => {
  const result = new Map<string, InstanceView>()
  for (const instance of instances.value) {
    if (!['starting', 'running', 'stopping'].includes(instance.state)) continue
    if (!result.has(instance.app_id)) result.set(instance.app_id, instance)
  }
  return result
})
async function refresh() {
  try { [apps.value, instances.value] = await Promise.all([getPublicApps(), getGuestInstances()]) }
  catch { message.error('公共应用加载失败') }
}
async function enter(app: ApplicationCard, viewOnly = false) {
  busy.value = app.app_id
  try {
    const current = activeByApp.value.get(app.app_id)
    if (current) {
      const instance = current.reconnectable ? current : await waitForGuestInstance(current.instance_id)
      await openGuestInstance(instance, sessionStorage.getItem(`px_guest_nonce_app_${app.app_id}`) || crypto.randomUUID(), viewOnly)
    } else {
      const started = await startPublicApp(app.app_id)
      const instance = started.instance.reconnectable
        ? started.instance
        : await waitForGuestInstance(started.instance.instance_id)
      await openGuestInstance(instance, started.clientNonce, viewOnly)
    }
  } catch (error: any) {
    message.error(error?.response?.data?.message || error?.message || '应用启动失败')
  } finally { busy.value = '' }
}
function stop(app: ApplicationCard) {
  const instance = activeByApp.value.get(app.app_id)
  if (!instance) return
  Modal.confirm({ title: `停止「${app.name}」？`, okType: 'danger', async onOk() { await stopGuestInstance(instance.instance_id); await refresh() } })
}
onMounted(() => { void refresh(); timer = window.setInterval(() => void refresh(), 5000) })
onUnmounted(() => window.clearInterval(timer))
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
          <a-card hoverable><a-card-meta :title="app.name" :description="activeByApp.get(app.app_id) ? `我的实例：${activeByApp.get(app.app_id)?.state}` : '公开应用'" />
            <div class="mt-5 flex justify-end"><a-space><a-button :loading="busy === app.app_id" :disabled="activeByApp.get(app.app_id)?.state === 'stopping'" @click="enter(app, true)">仅观看</a-button><a-button v-if="activeByApp.get(app.app_id)" danger :disabled="activeByApp.get(app.app_id)?.state === 'stopping'" @click="stop(app)">停止</a-button><a-button type="primary" :loading="busy === app.app_id" :disabled="activeByApp.get(app.app_id)?.state === 'stopping'" @click="enter(app)">{{ activeByApp.get(app.app_id) ? '进入' : '启动并进入' }}</a-button></a-space></div>
          </a-card>
        </a-col>
      </a-row>
    </div>
  </main>
</template>
