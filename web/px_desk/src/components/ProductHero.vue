<script setup lang="ts">
/**
 * 产品详情页 Hero：logo + 等宽产品名（主题色）+ 状态行 + 定位语 + 主题色光晕。
 * 主题色通过父级设置的 --pa CSS 变量驱动。
 */
defineProps<{
  logo: string
  name: string
  status: string
  tagline: string
}>()
</script>

<template>
  <section class="product-hero relative overflow-hidden">
    <div class="section-container relative z-10 flex flex-col items-center gap-5 py-14 text-center md:py-20">
      <div class="cyber-panel cyber-corners p-4">
        <img :src="logo" :alt="name" class="h-20 w-20 md:h-24 md:w-24" />
      </div>

      <h1
        class="font-tech font-bold tracking-[0.14em] text-cyber-text uppercase"
        style="font-size: clamp(1.5rem, 2.8vw, 3rem)"
      >
        {{ name }}
      </h1>

      <p class="font-tech text-xs md:text-sm tracking-[0.14em]" style="color: var(--pa)">
        {{ status }}<span class="cursor-blink">▌</span>
      </p>

      <p class="max-w-2xl text-base text-cyber-muted">{{ tagline }}</p>

      <div class="mt-3 flex flex-wrap justify-center gap-4">
        <slot />
      </div>
    </div>
  </section>
</template>

<style scoped>
.product-hero {
  background:
    radial-gradient(ellipse 60% 55% at 50% 38%, color-mix(in srgb, var(--pa) 14%, transparent), transparent 70%);
}
.cursor-blink {
  animation: blink 1s step-end infinite;
}
@keyframes blink {
  50% {
    opacity: 0;
  }
}
</style>
