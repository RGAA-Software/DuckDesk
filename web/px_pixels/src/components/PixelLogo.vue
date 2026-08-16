<script setup lang="ts">
/* PIXELS Logo：云朵组合图标（大云 + 浅色小云 + 上方圆形）
   参考构图：iconfont 云朵样式，已去掉底部三个竖条
   配色：主题绿三层（浅绿高光 / 主绿 / 深绿），支持暗色模式 */
withDefaults(defineProps<{ size?: number }>(), { size: 30 })

/* 内容区域：x 82..942, y 91..671 → 居中裁剪 viewBox（上下/左右边距对称，保证视觉居中） */
const W = 880
const H = 600
</script>

<template>
  <svg
    :width="size * (W / H)"
    :height="size"
    viewBox="72 81 880 600"
    role="img"
    aria-label="PIXELS Logo"
  >
    <!-- 左后浅色小云 -->
    <path
      d="M496.46 505.46v165.78H247.78C156.22 671.24 82 597.02 82 505.46s74.22-165.79 165.78-165.79 165.79 74.23 165.79 165.79h82.89z"
      fill="var(--px-cloud-light)"
    />
    <!-- 主云 -->
    <path
      d="M807.3 381.12c0 93.99-44.69 177.53-113.98 230.55v59.57H517.18c-160.23 0-290.12-129.89-290.12-290.12S356.95 91 517.18 91 807.3 220.89 807.3 381.12z"
      fill="var(--px-green)"
    />
    <!-- 上方圆形 -->
    <path
      d="M714.05 443.29m-227.95 0a227.95 227.95 0 1 0 455.9 0 227.95 227.95 0 1 0-455.9 0Z"
      fill="var(--px-cloud-dark)"
    />
  </svg>
</template>
