<script setup lang="ts">
/* PIXELS 字标：Press Start 2P 像素字体（@fontsource 本地打包，OFL 开源许可） */
withDefaults(defineProps<{ height?: number }>(), { height: 16 })
</script>

<template>
  <span class="pixels-word" :style="{ fontSize: `${height}px` }">PIXELS</span>
</template>

<style scoped>
.pixels-word {
  display: inline-block;
  font-family: 'Press Start 2P', 'Courier New', monospace;
  line-height: 1;
  letter-spacing: 2px;
  color: currentColor;
  white-space: nowrap;
  user-select: none;
  /* 垂直对齐微调：Press Start 2P 字形中心比行框中心高 0.0625em，
     按需调整此值（正值向下，负值向上） */
  transform: translateY(0);
}
</style>
