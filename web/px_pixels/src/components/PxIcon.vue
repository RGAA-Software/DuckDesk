<script setup lang="ts">
import { computed } from 'vue'

/* 像素图标：以字符画（'x' 为填充块）渲染为 SVG */
const props = withDefaults(
  defineProps<{ art: string[]; size?: number }>(),
  { size: 40 }
)

const cells = computed(() => {
  const filledCells: { x: number; y: number }[] = []
  props.art.forEach((row, y) => {
    for (let x = 0; x < row.length; x++) {
      if (row[x] === 'x') filledCells.push({ x, y })
    }
  })
  return filledCells
})

const iconWidth = computed(() => Math.max(...props.art.map((row) => row.length)))
const iconHeight = computed(() => props.art.length)
</script>

<template>
  <svg
    :width="size"
    :height="size"
    :viewBox="`0 0 ${iconWidth} ${iconHeight}`"
    shape-rendering="crispEdges"
    aria-hidden="true"
  >
    <rect v-for="(cell, cellIndex) in cells" :key="cellIndex" :x="cell.x" :y="cell.y" width="1" height="1" fill="currentColor" />
  </svg>
</template>
