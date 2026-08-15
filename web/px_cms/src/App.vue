<script setup lang="ts">
import { RouterView } from 'vue-router'

// App.vue setup
import { useWsStore } from '@/stores/ws'
import { HOST_PORT } from '@/http.ts'
import { generateConnectionToken } from '@/util/auth_token.ts'
import { useTheme } from '@/composables/useTheme'

const wsStore = useWsStore()
const appkey = localStorage.getItem('appkey') || ''
const tokenInfo = generateConnectionToken(appkey)
const wsProtocol = window.location.protocol === 'https:' ? 'wss' : 'ws'
const url = `${wsProtocol}://${HOST_PORT}/cms/website?appkey=${appkey}&token=${tokenInfo.token}&ts=${tokenInfo.ts}&nonce=${tokenInfo.nonce}`
console.log(url)
wsStore.connect(url)

const { themeConfig } = useTheme()
</script>

<template>
  <a-config-provider :theme="themeConfig">
    <RouterView />
  </a-config-provider>
</template>

<style scoped></style>
