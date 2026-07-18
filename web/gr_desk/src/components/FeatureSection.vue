<script setup lang="ts">
export interface FeatureItem {
  title: string
  desc: string
}

const props = withDefaults(
  defineProps<{
    title: string
    items: FeatureItem[]
    image: string
    /** 图片在左侧（默认在右侧，交替排布） */
    imageLeft?: boolean
    /** 编号（01~05，HUD 装饰） */
    index?: string
  }>(),
  {
    imageLeft: false,
    index: '01',
  },
)
</script>

<template>
  <section v-reveal class="section-container py-10 md:py-14">
    <div
      class="flex flex-col items-center gap-8 md:gap-16"
      :class="props.imageLeft ? 'md:flex-row-reverse' : 'md:flex-row'"
    >
      <!-- 文案区 -->
      <div class="w-full md:w-1/2">
        <div class="cyber-label mb-3">// {{ props.index }}</div>
        <h2 class="cyber-title text-2xl md:text-3xl font-bold text-cyber-text">{{ props.title }}</h2>

        <ul class="mt-7 flex flex-col gap-5">
          <li v-for="(item, i) in props.items" :key="i">
            <div class="flex items-center gap-3">
              <span class="cyber-dot"></span>
              <span class="font-tech text-base font-bold tracking-wider text-cyber-text">{{ item.title }}</span>
            </div>
            <p class="mt-1 pl-5 text-sm text-cyber-muted">{{ item.desc }}</p>
          </li>
        </ul>

        <slot />
      </div>

      <!-- 配图区：切角屏幕框 -->
      <div class="w-full md:w-1/2 flex justify-center">
        <div class="cyber-panel cyber-corners w-full max-w-md p-3">
          <img :src="props.image" :alt="props.title" class="w-full" />
        </div>
      </div>
    </div>
  </section>
</template>
