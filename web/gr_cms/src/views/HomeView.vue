<script setup lang="ts">
import AsideView from '@/views/AsideView.vue'
import HeaderView from '@/views/HeaderView.vue'
import { useRoute } from 'vue-router'
import { computed, onMounted, ref } from 'vue'
import type { Authorization } from '@/entity/authorization.ts'
import { queryAuthorization } from '@/model/auth_api.ts'
import { formatTimeToDays } from '@/util/time.ts'
const route = useRoute()

const headerTitle = computed(() => {
  return (route.meta.title as string) ?? ''
})

const auth = ref<Authorization | null>(null)
const authUsedInfo = computed(() => {
  if (!auth.value) {
    return ''
  }
  let totalDays = auth.value.days.toString()
  if (auth.value.days > 3650) {
    totalDays = '∞'
  }
  return (
    '授权已使用: ' +
    formatTimeToDays(auth.value.used_time_ms) +
    ' / ' +
    totalDays +
    '天,' +
    '最大连接数: ' +
    auth.value.max_streams
  )
})

onMounted(async () => {
  auth.value = await queryAuthorization()
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
