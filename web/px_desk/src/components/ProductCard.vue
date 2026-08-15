<script setup lang="ts">
/** 门户主页产品矩阵卡：主题色描边 hover + logo + 定位 + 特性点 + 详情链接。 */
defineProps<{
  logo: string
  name: string
  tagline: string
  points: string[]
  accent: string
  to: string
  viewDetails: string
}>()
</script>

<template>
  <RouterLink :to="to" class="product-card cyber-panel group flex flex-col p-6" :style="{ '--pa': accent }">
    <div class="flex items-center gap-4">
      <img :src="logo" :alt="name" class="h-14 w-14" />
      <div>
        <h3 class="font-tech text-lg font-bold tracking-[0.1em] text-cyber-text uppercase">{{ name }}</h3>
        <p class="mt-1 text-xs text-cyber-muted">{{ tagline }}</p>
      </div>
    </div>

    <ul class="mt-5 flex flex-col gap-2.5">
      <li v-for="(point, i) in points" :key="i" class="flex items-center gap-2.5 text-sm text-cyber-muted">
        <span class="card-dot"></span>
        <span>{{ point }}</span>
      </li>
    </ul>

    <div class="mt-auto pt-6">
      <span class="card-link font-tech text-xs font-bold tracking-[0.14em] uppercase">
        {{ viewDetails }} →
      </span>
    </div>
  </RouterLink>
</template>

<style scoped>
.product-card {
  transition: transform 0.25s;
  cursor: pointer;
}
.product-card::before {
  transition: background 0.25s;
}
.product-card:hover {
  transform: translateY(-4px);
}
.product-card:hover::before {
  background: var(--pa);
}
.card-dot {
  width: 6px;
  height: 6px;
  background: var(--pa);
  flex-shrink: 0;
}
.card-link {
  color: var(--pa);
}
</style>
