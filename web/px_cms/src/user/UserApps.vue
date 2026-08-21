<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { message } from 'ant-design-vue'
import { getApps, openInstance, startApp, waitForInstance, type ApplicationCard } from './api'
const apps = ref<ApplicationCard[]>([])
const busy = ref('')
const loading = ref(false)
let refreshing = false
let timer = 0
async function refresh(showLoading = false) {
  if (refreshing) return
  refreshing = true
  if (showLoading) loading.value = true
  try { apps.value = await getApps() }
  catch { if (showLoading) message.error('应用列表加载失败') }
  finally { refreshing = false; loading.value = false }
}
async function enter(app: ApplicationCard) {
  busy.value = app.app_id
  try {
    if (app.running_instance?.reconnectable) {
      await openInstance({ instance_id: app.running_instance.instance_id, app_id: app.app_id, app_name: app.name, state: app.running_instance.state, created_at: 0, reconnectable: true })
    } else if (app.running_instance?.state === 'starting') {
      await openInstance(await waitForInstance(app.running_instance.instance_id))
    } else {
      const { instance, clientNonce } = await startApp(app.app_id)
      await openInstance(instance.reconnectable ? instance : await waitForInstance(instance.instance_id), clientNonce)
    }
  } catch (error: any) { message.error(error?.response?.data?.message || error?.message || '应用启动失败') }
  finally { busy.value = ''; void refresh() }
}
function stateText(state?: string) {
  return ({ running: '运行中', starting: '启动中', stopping: '停止中', failed: '失败', stopped: '未运行' } as Record<string, string>)[state || 'stopped'] || state
}
onMounted(() => { void refresh(true); timer = window.setInterval(() => void refresh(), 3000) })
onUnmounted(() => window.clearInterval(timer))
</script>
<template>
  <a-card title="云端应用">
    <template #extra><a-button :loading="loading" @click="refresh(true)">刷新</a-button></template>
    <a-empty v-if="!loading && apps.length === 0" description="没有可用应用" />
    <a-row v-else :gutter="[16,16]">
      <a-col v-for="app in apps" :key="app.app_id" :xs="24" :md="12" :xl="8">
        <a-card hoverable class="h-full overflow-hidden">
          <template #cover>
            <div class="flex h-36 items-center justify-center bg-slate-100">
              <img v-if="app.cover_url" :src="app.cover_url" :alt="app.name" class="h-full w-full object-cover" />
              <span v-else class="text-4xl text-slate-300">▣</span>
            </div>
          </template>
          <a-card-meta :title="app.name" :description="app.access_mode === 'public' ? '公开应用' : '用户组专属应用'" />
          <div class="mt-5 flex items-center justify-between">
            <a-tag :color="app.running_instance?.state === 'running' ? 'green' : app.running_instance?.state === 'starting' ? 'blue' : 'default'">{{ stateText(app.running_instance?.state) }}</a-tag>
            <a-button type="primary" :loading="busy === app.app_id" :disabled="app.running_instance?.state === 'stopping'" @click="enter(app)">{{ app.running_instance && ['running','starting'].includes(app.running_instance.state) ? '进入' : '启动' }}</a-button>
          </div>
        </a-card>
      </a-col>
    </a-row>
  </a-card>
</template>
