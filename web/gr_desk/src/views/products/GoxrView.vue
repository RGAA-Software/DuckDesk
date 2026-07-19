<script setup lang="ts">
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'

import goxrLogo from '@/assets/products/goxr-logo.png'
import goxrDashboard from '@/assets/products/xr_01.png'
import goxrWall from '@/assets/products/xr_02.png'

import ContactUs from '@/components/ContactUs.vue'
import ProductHero from '@/components/ProductHero.vue'
import ProductSection from '@/components/ProductSection.vue'
import FeatureGrid from '@/components/FeatureGrid.vue'

const { t, tm } = useI18n()

interface KV {
  title?: string
  name?: string
  desc: string
}

const pillars = computed(() => tm('products.goxr.pillars') as KV[])
const features = computed(() =>
  (tm('products.goxr.features') as KV[]).map((f) => ({ title: f.title ?? '', desc: f.desc })),
)
const diffs = computed(() => tm('products.goxr.diffs') as KV[])
const scenes = computed(() => tm('products.goxr.scenes') as string[])

const contactUsVisible = ref(false)
const goContactUs = () => {
  contactUsVisible.value = true
}
</script>

<template>
  <div style="--pa: #2ac7c4">
    <ContactUs v-model="contactUsVisible" />

    <!-- 产品 Hero -->
    <ProductHero
      :logo="goxrLogo"
      :name="t('productNames.goxr')"
      :status="t('products.goxr.status')"
      :tagline="t('products.goxr.tagline')"
    >
      <el-button type="primary" class="!h-11 !px-8" @click="goContactUs">
        {{ t('products.common.consult') }}
      </el-button>
    </ProductHero>

    <!-- 三端架构 -->
    <ProductSection index="01" :title="t('products.goxr.archTitle')">
      <div class="grid gap-4 md:grid-cols-3">
        <div v-for="(pillar, i) in pillars" :key="i" v-reveal="i * 120" class="pillar-card cyber-panel p-6">
          <div class="flex items-center gap-3">
            <span class="pillar-num font-tech">0{{ i + 1 }}</span>
            <h3 class="font-tech text-base font-bold tracking-wider text-cyber-text">{{ pillar.name }}</h3>
          </div>
          <p class="mt-3 text-sm leading-relaxed text-cyber-muted">{{ pillar.desc }}</p>
        </div>
      </div>
    </ProductSection>

    <!-- 核心功能 -->
    <ProductSection index="02" :title="t('products.common.features')">
      <FeatureGrid :items="features" />
    </ProductSection>

    <!-- 差异化能力 -->
    <ProductSection index="03" :title="t('products.goxr.diffTitle')">
      <div class="flex flex-col gap-4">
        <div
          v-for="(diff, i) in diffs"
          :key="i"
          v-reveal="i * 100"
          class="cyber-panel flex flex-col gap-2 p-5 md:flex-row md:items-center md:gap-6"
        >
          <h3 class="diff-title shrink-0 font-tech text-sm font-bold tracking-wider md:w-64">{{ diff.title }}</h3>
          <p class="text-sm leading-relaxed text-cyber-muted">{{ diff.desc }}</p>
        </div>
      </div>
    </ProductSection>

    <!-- 界面预览 -->
    <ProductSection index="04" :title="t('products.common.preview')">
      <div class="flex flex-col gap-6">
        <div v-reveal class="cyber-panel cyber-corners preview-shot p-2">
          <img :src="goxrDashboard" alt="GoXR Manager Dashboard" class="w-full" loading="lazy" />
        </div>
        <div v-reveal class="cyber-panel cyber-corners preview-shot p-2">
          <img :src="goxrWall" alt="GoXR Manager Video Wall" class="w-full" loading="lazy" />
        </div>
      </div>
    </ProductSection>

    <!-- 适用场景 -->
    <ProductSection index="05" :title="t('products.common.scenes')">
      <div class="grid grid-cols-2 gap-4 md:grid-cols-4">
        <div v-for="(scene, i) in scenes" :key="i" v-reveal="i * 80" class="scene-card cyber-panel p-5 text-center">
          <span class="font-tech text-sm font-bold tracking-wider text-cyber-text">{{ scene }}</span>
        </div>
      </div>
    </ProductSection>

    <!-- CTA -->
    <section v-reveal class="section-container mt-16 md:mt-24">
      <div class="cyber-panel cyber-corners cta-panel flex flex-col items-center gap-6 px-6 py-12 text-center">
        <div class="cyber-label">// CONTACT</div>
        <h2 class="font-tech text-xl md:text-2xl font-bold tracking-wider text-cyber-text">
          {{ t('productNames.goxr') }}
        </h2>
        <div class="flex flex-wrap justify-center gap-4">
          <el-button type="primary" class="!h-11 !px-8" @click="goContactUs">
            {{ t('products.common.consult') }}
          </el-button>
        </div>
      </div>
    </section>
  </div>
</template>

<style scoped>
.pillar-num {
  font-size: 22px;
  color: var(--pa);
}
.diff-title {
  color: var(--pa);
}
.pillar-card:hover::before,
.scene-card:hover::before {
  background: var(--pa);
}
.pillar-card,
.scene-card {
  transition: transform 0.2s;
}
.pillar-card:hover,
.scene-card:hover {
  transform: translateY(-3px);
}
.preview-shot::before {
  background: var(--pa);
  opacity: 0.5;
}
.cta-panel::before {
  background: color-mix(in srgb, var(--pa) 55%, transparent);
}
</style>
