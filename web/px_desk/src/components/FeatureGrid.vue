<script setup lang="ts">
/** 功能卡片矩阵：方形主题色标记 + 标题/描述。主题色由父级 --pa 驱动。 */
export interface FeatureCardItem {
  title: string
  desc: string
}

defineProps<{
  items: FeatureCardItem[]
}>()
</script>

<template>
  <div class="grid gap-4 sm:grid-cols-2 lg:grid-cols-4">
    <div v-for="(item, i) in items" :key="i" v-reveal="(i % 4) * 80" class="feature-card cyber-panel p-5">
      <span class="feature-dot"></span>
      <h3 class="mt-3 font-tech text-sm font-bold tracking-wider text-cyber-text">{{ item.title }}</h3>
      <p class="mt-2 text-xs leading-relaxed text-cyber-muted">{{ item.desc }}</p>
    </div>
  </div>
</template>

<style scoped>
.feature-dot {
  display: inline-block;
  width: 7px;
  height: 7px;
  background: var(--pa, var(--brand));
}
.feature-card {
  transition: transform 0.2s;
}
.feature-card:hover {
  transform: translateY(-3px);
}
.feature-card:hover::before {
  background: var(--pa, var(--brand-dark));
}
</style>
