<script setup lang="ts">
import { RouterView } from 'vue-router'

// App.vue setup
import { useWsStore } from '@/stores/ws'
import { HOST_PORT } from '@/http.ts'

const wsStore = useWsStore()
const url = 'ws://' + HOST_PORT + '/spvr/website?appkey=' + localStorage.getItem('appkey')
console.log(url)
wsStore.connect(url)
</script>

<template>
  <div>
    <RouterView />
  </div>
</template>

<style scoped></style>
