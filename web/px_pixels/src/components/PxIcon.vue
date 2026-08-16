<script setup lang="ts">
import { computed } from 'vue'

/* 像素图标：以字符画（'x' 为填充块）渲染为 SVG */
const props = withDefaults(
  defineProps<{ art: string[]; size?: number }>(),
  { size: 40 }
)

const cells = computed(() => {
  const out: { x: number; y: number }[] = []
  props.art.forEach((row, y) => {
    for (let x = 0; x < row.length; x++) {
      if (row[x] === 'x') out.push({ x, y })
    }
  })
  return out
})

const w = computed(() => Math.max(...props.art.map((r) => r.length)))
const h = computed(() => props.art.length)
</script>

<template>
  <svg
    :width="size"
    :height="size"
    :viewBox="`0 0 ${w} ${h}`"
    shape-rendering="crispEdges"
    aria-hidden="true"
  >
    <rect v-for="(c, i) in cells" :key="i" :x="c.x" :y="c.y" width="1" height="1" fill="currentColor" />
  </svg>
</template>
