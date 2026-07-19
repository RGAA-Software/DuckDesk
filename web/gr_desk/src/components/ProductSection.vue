<script setup lang="ts">
/** 产品页 section 容器：// NN 编号 + 主题色竖条标题。主题色由父级 --pa 驱动。 */
withDefaults(
  defineProps<{
    index: string
    title: string
  }>(),
  { index: '01' },
)
</script>

<template>
  <section v-reveal class="section-container mt-14 md:mt-20">
    <div class="cyber-label mb-3">// {{ index }}</div>
    <h2 class="product-title text-xl md:text-2xl font-bold text-cyber-text">{{ title }}</h2>
    <div class="mt-8">
      <slot />
    </div>
  </section>
</template>

<style scoped>
.product-title {
  font-family: var(--font-tech);
  letter-spacing: 0.08em;
  text-transform: uppercase;
  display: flex;
  align-items: center;
}
.product-title::before {
  content: '';
  width: 3px;
  height: 1em;
  background: var(--pa, var(--brand));
  margin-right: 12px;
  flex-shrink: 0;
}
</style>
