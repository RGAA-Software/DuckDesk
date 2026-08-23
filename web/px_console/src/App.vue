<script setup lang="ts">
import { RouterView } from 'vue-router'

import { useTheme } from '@/composables/useTheme'

const { themeConfig } = useTheme()
</script>

<template>
  <a-config-provider :theme="themeConfig">
    <RouterView />
  </a-config-provider>
</template>

<style scoped></style>
