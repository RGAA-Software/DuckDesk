<script setup lang="ts">
import AsideView from '@/views/AsideView.vue'
import HeaderView from '@/views/HeaderView.vue'
import { useRoute } from 'vue-router'
import { computed, onMounted } from 'vue'
import { refreshSharedAuthorization, sharedAuthorization } from '@/model/auth_state.ts'
import { formatDurationHMS } from '@/util/time.ts'
const route = useRoute()

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
  await refreshSharedAuthorization()
})
</script>
<template>
  <el-container direction="horizontal" class="min-h-screen">
    <el-aside width="160px" class="!bg-blue-50">
      <AsideView />
    </el-aside>
    <div class="w-2" />
    <el-container direction="vertical">
      <el-header class="!p-0">
        <HeaderView :title="headerTitle" :authInfo="authUsedInfo" />
      </el-header>
      <el-main class="!p-0">
        <RouterView />
      </el-main>
    </el-container>
  </el-container>
</template>

<style scoped></style>
