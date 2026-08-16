<script setup lang="ts">
/* PIXELS Logo：第一版 8×8 像素云朵（单色，跟随主题色） */
withDefaults(defineProps<{ size?: number }>(), { size: 30 })

const blocks = [
  { x: 4, y: 0, w: 4, h: 2 },
  { x: 2, y: 2, w: 8, h: 2 },
  { x: 0, y: 4, w: 12, h: 4 },
  { x: 2, y: 8, w: 8, h: 2 },
  { x: 4, y: 10, w: 4, h: 2 },
]
</script>

<template>
  <svg
    :width="size"
    :height="size"
    viewBox="0 0 16 16"
    shape-rendering="crispEdges"
    aria-label="PIXELS Logo"
  >
    <rect
      v-for="(b, i) in blocks"
      :key="i"
      :x="b.x"
      :y="b.y"
      :width="b.w"
      :height="b.h"
      fill="currentColor"
    />
  </svg>
</template>
