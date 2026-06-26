<script setup lang="ts">
import { RouterView } from 'vue-router'

// App.vue setup
import { useWsStore } from '@/stores/ws'
import { HOST_PORT } from '@/http.ts'
import { generateConnectionToken } from '@/util/auth_token.ts'

const wsStore = useWsStore()
const appkey = localStorage.getItem('appkey') || ''
const tokenInfo = generateConnectionToken(appkey)
const url = `ws://${HOST_PORT}/spvr/website?appkey=${appkey}&token=${tokenInfo.token}&ts=${tokenInfo.ts}&nonce=${tokenInfo.nonce}`
console.log(url)
wsStore.connect(url)
</script>

<template>
  <div>
    <RouterView />
  </div>
</template>

<style scoped></style>
