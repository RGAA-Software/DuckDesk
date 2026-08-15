<script setup lang="ts">
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'

import cmonLogo from '@/assets/products/cybermonitor-logo.svg'
import cmonOverview from '@/assets/products/cmon-ui-overview.jpg'
import cmonGpu from '@/assets/products/cmon-ui-gpu.jpg'

import ProductHero from '@/components/ProductHero.vue'
import ProductSection from '@/components/ProductSection.vue'
import FeatureGrid from '@/components/FeatureGrid.vue'

const { t, tm } = useI18n()

interface KV {
  title?: string
  name?: string
  desc: string
}

const features = computed(() =>
  (tm('products.cybermonitor.features') as KV[]).map((f) => ({ title: f.title ?? '', desc: f.desc })),
)
const chClient = computed(() => tm('products.cybermonitor.chClient') as KV)
const chHost = computed(() => tm('products.cybermonitor.chHost') as KV)
const scenes = computed(() => tm('products.cybermonitor.scenes') as string[])

</script>

<template>
  <div style="--pa: #7548d8">

    <!-- 产品 Hero -->
    <ProductHero
      :logo="cmonLogo"
      :name="t('productNames.cybermonitor')"
      :status="t('products.cybermonitor.status')"
      :tagline="t('products.cybermonitor.tagline')"
    >
    </ProductHero>

    <!-- 核心功能 -->
    <ProductSection index="01" :title="t('products.common.features')">
      <FeatureGrid :items="features" />
    </ProductSection>

    <!-- Client / Host 架构 -->
    <ProductSection index="02" :title="t('products.cybermonitor.chTitle')">
      <div class="flex flex-col items-stretch gap-4 md:flex-row md:items-center">
        <div v-reveal class="ch-card cyber-panel flex-1 p-6">
          <div class="cyber-label mb-2">// CLIENT</div>
          <h3 class="font-tech text-base font-bold tracking-wider text-cyber-text">{{ chClient.name }}</h3>
          <p class="mt-3 text-sm leading-relaxed text-cyber-muted">{{ chClient.desc }}</p>
        </div>

        <div v-reveal="100" class="ch-arrow font-tech">⇄</div>

        <div v-reveal="200" class="ch-card cyber-panel flex-1 p-6">
          <div class="cyber-label mb-2">// HOST</div>
          <h3 class="font-tech text-base font-bold tracking-wider text-cyber-text">{{ chHost.name }}</h3>
          <p class="mt-3 text-sm leading-relaxed text-cyber-muted">{{ chHost.desc }}</p>
        </div>
      </div>
    </ProductSection>

    <!-- 界面预览 -->
    <ProductSection index="03" :title="t('products.common.preview')">
      <div class="flex flex-col gap-6">
        <div v-reveal class="cyber-panel cyber-corners preview-shot p-2">
          <img :src="cmonOverview" alt="CyberMonitor Overview" class="w-full" loading="lazy" />
        </div>
        <div v-reveal class="cyber-panel cyber-corners preview-shot p-2">
          <img :src="cmonGpu" alt="CyberMonitor GPU" class="w-full" loading="lazy" />
        </div>
      </div>
    </ProductSection>

    <!-- 适用场景 -->
    <ProductSection index="04" :title="t('products.common.scenes')">
      <div class="grid grid-cols-2 gap-4 md:grid-cols-4">
        <div v-for="(scene, i) in scenes" :key="i" v-reveal="i * 80" class="scene-card cyber-panel p-5 text-center">
          <span class="font-tech text-sm font-bold tracking-wider text-cyber-text">{{ scene }}</span>
        </div>
      </div>
    </ProductSection>

  </div>
</template>

<style scoped>
.ch-card:hover::before,
.scene-card:hover::before {
  background: var(--pa);
}
.ch-card,
.scene-card {
  transition: transform 0.2s;
}
.ch-card:hover,
.scene-card:hover {
  transform: translateY(-3px);
}
.ch-arrow {
  color: var(--pa);
  font-size: 28px;
  text-align: center;
}
.preview-shot::before {
  background: var(--pa);
  opacity: 0.5;
}
.cta-panel::before {
  background: color-mix(in srgb, var(--pa) 55%, transparent);
}
</style>
