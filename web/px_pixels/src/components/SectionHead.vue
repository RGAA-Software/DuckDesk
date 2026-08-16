<script setup lang="ts">
defineProps<{ eyebrow: string; title: string; desc?: string }>()
</script>

<template>
  <div class="section-head">
    <p class="px-eyebrow">{{ eyebrow }}</p>
    <h2 class="px-h2">{{ title }}</h2>
    <p v-if="desc" class="px-sub">{{ desc }}</p>
  </div>
</template>
