<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { message } from 'ant-design-vue'
import { getApps, openInstance, startApp, waitForInstance, type ApplicationCard } from './api'
const apps = ref<ApplicationCard[]>([])
const busy = ref('')
async function refresh() { apps.value = await getApps() }
async function enter(app: ApplicationCard) {
  busy.value = app.app_id
  try {
    if (app.running_instance?.reconnectable) {
      await openInstance({ instance_id: app.running_instance.instance_id, app_id: app.app_id, app_name: app.name, state: app.running_instance.state, created_at: 0, reconnectable: true })
    } else {
      const { instance, clientNonce } = await startApp(app.app_id)
      await openInstance(instance.reconnectable ? instance : await waitForInstance(instance.instance_id), clientNonce)
    }
  } catch (error: any) { message.error(error?.message || '应用启动失败') }
  finally { busy.value = '' }
}
onMounted(refresh)
</script>
<template>
  <a-card title="云端应用">
    <template #extra><a-button @click="refresh">刷新</a-button></template>
    <a-empty v-if="apps.length === 0" description="没有可用应用" />
    <a-row v-else :gutter="[16,16]">
      <a-col v-for="app in apps" :key="app.app_id" :xs="24" :md="12" :xl="8">
        <a-card hoverable>
          <a-card-meta :title="app.name" :description="app.access_mode === 'public' ? '公开应用' : '用户组专属应用'" />
          <div class="mt-5 flex items-center justify-between">
            <a-tag :color="app.running_instance ? 'blue' : 'default'">{{ app.running_instance?.state || '未运行' }}</a-tag>
            <a-button type="primary" :loading="busy === app.app_id" @click="enter(app)">{{ app.running_instance?.reconnectable ? '进入' : '启动' }}</a-button>
          </div>
        </a-card>
      </a-col>
    </a-row>
  </a-card>
</template>
