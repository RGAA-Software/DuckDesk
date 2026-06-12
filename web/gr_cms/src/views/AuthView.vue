<script setup lang="ts">
import { useRouter } from 'vue-router'

const router = useRouter()

function authorize() {
  // 这里写你的授权逻辑
  const ok = true // 假设成功

  if (ok) {
    //router.replace('/main')
    router.push('/main')
  }
}
</script>

<template>
  <div class="bg-amber-300 w-100 h-10">
    <el-text class="!text-3xl">DOCS</el-text>
  </div>
  <div>
    <h3>授权页面</h3>
    <el-button @click="authorize">Authorize</el-button>
  </div>
</template>

<style scoped></style>
