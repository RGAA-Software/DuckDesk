<script setup lang="ts">
import AsideView from '@/views/AsideView.vue'
import HeaderView from '@/views/HeaderView.vue'
import { useRoute } from 'vue-router'
import { computed, onMounted } from 'vue'
import { refreshSharedAuthorization, sharedAuthorization } from '@/model/auth_state.ts'
import { formatDurationHMS } from '@/util/time.ts'
import { useTheme } from '@/composables/useTheme'
import { useWsStore } from '@/stores/ws'
import { HOST_PORT } from '@/http.ts'
const route = useRoute()
const { isDark } = useTheme()
const wsStore = useWsStore()

const headerTitle = computed(() => {
  return (route.meta.title as string) ?? ''
})

const authUsedInfo = computed(() => {
  const auth = sharedAuthorization.value
  if (!auth) {
    return ''
  }
  let totalDays = auth.days.toString()
  if (auth.days > 3650) {
    totalDays = '∞'
  }
  return (
    '授权已使用: ' +
    formatDurationHMS(auth.used_time_ms) +
    ' / ' +
    totalDays +
    '天,' +
    '最大连接数: ' +
    auth.max_streams
  )
})

onMounted(async () => {
  const wsProtocol = window.location.protocol === 'https:' ? 'wss' : 'ws'
  wsStore.connect(`${wsProtocol}://${HOST_PORT}/console/website`)
  await refreshSharedAuthorization()
})
</script>

<template>
  <a-layout class="min-h-screen">
    <a-layout-sider width="160px" :theme="isDark ? 'dark' : 'light'">
      <AsideView />
    </a-layout-sider>
    <a-layout>
      <a-layout-header
        :style="{
          background: isDark ? '#141414' : '#fff',
          padding: 0,
          height: 'auto',
          lineHeight: 'normal',
        }"
      >
        <HeaderView :title="headerTitle" :authInfo="authUsedInfo" />
      </a-layout-header>
      <a-layout-content>
        <RouterView />
      </a-layout-content>
    </a-layout>
  </a-layout>
</template>

<style scoped></style>
