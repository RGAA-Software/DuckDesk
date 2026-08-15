<script setup lang="ts">
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'

import iconLogo from '@/assets/icon/ic_trans_icon_blue.png'
import goxrLogo from '@/assets/products/goxr-logo.png'
import cmonLogo from '@/assets/products/cybermonitor-logo.svg'
import godeskShot from '@/assets/preview/shot_1.png'
import goxrShot from '@/assets/products/xr_01.png'
import cmonShot from '@/assets/products/cmon-ui-overview.jpg'

import DotGlobe from '@/components/DotGlobe.vue'
import ContactUs from '@/components/ContactUs.vue'
import ProductCard from '@/components/ProductCard.vue'

const { t, tm } = useI18n()
const router = useRouter()

interface PortalCard {
  key: string
  tagline: string
  points: string[]
}

const PRODUCT_META: Record<string, { logo: string; accent: string; path: string; shot: string }> = {
  godesk: { logo: iconLogo, accent: '#2f8fff', path: '/products/godesk', shot: godeskShot },
  goxr: { logo: goxrLogo, accent: '#2ac7c4', path: '/products/goxr', shot: goxrShot },
  cybermonitor: { logo: cmonLogo, accent: '#7548d8', path: '/products/cybermonitor', shot: cmonShot },
}

const cards = computed(() =>
  (tm('portal.cards') as PortalCard[]).map((c) => ({
    ...c,
    name: t(`productNames.${c.key}`),
    ...PRODUCT_META[c.key],
  })),
)

const matrixRef = ref<HTMLElement>()
const scrollToMatrix = () => {
  matrixRef.value?.scrollIntoView({ behavior: 'smooth', block: 'start' })
}

const contactUsVisible = ref(false)
const goContactUs = () => {
  contactUsVisible.value = true
}
</script>

<template>
  <ContactUs v-model="contactUsVisible" />

  <!-- Hero：HUD 网格 + 点阵地球 + 覆盖式大标题 -->
  <section class="bg-hud-grid relative flex min-h-[68vh] flex-col items-center justify-center overflow-hidden">
    <h1
      class="pointer-events-none relative z-10 text-center font-tech font-bold tracking-[0.18em] whitespace-pre-wrap text-cyber-text"
      style="font-size: clamp(2.25rem, 4.2vw, 4.5rem)"
    >
      GO<span class="text-cyber-brand">DESK</span>
    </h1>
    <p class="pointer-events-none relative z-10 mt-4 font-tech text-xs md:text-sm tracking-[0.14em] text-cyber-brand">
      {{ t('portal.status') }}<span class="cursor-blink">▌</span>
    </p>
    <DotGlobe class="top-16 md:top-24" />
  </section>

  <!-- 品牌定位 + 入口 -->
  <section v-reveal class="section-container flex flex-col items-center text-center">
    <h2 class="font-bold" style="font-size: clamp(1.4rem, 2.2vw, 2.25rem)">
      <span class="text-cyber-text">{{ t('portal.slogan1') }}</span>
      <span class="mx-2 text-cyber-brand">//</span>
      <span style="color: #2ac7c4">{{ t('portal.slogan2') }}</span>
      <span class="mx-2 text-cyber-brand">//</span>
      <span style="color: #7548d8">{{ t('portal.slogan3') }}</span>
    </h2>

    <p class="mt-4 max-w-2xl text-base text-cyber-muted">{{ t('portal.subtitle') }}</p>

    <div class="mt-8 flex flex-wrap justify-center gap-4">
      <el-button type="primary" class="!h-11 !px-8" @click="scrollToMatrix">
        {{ t('portal.viewProducts') }}
      </el-button>
      <el-button class="!h-11 !px-8" @click="goContactUs">{{ t('footer.contactUs') }}</el-button>
    </div>
  </section>

  <!-- 产品矩阵 -->
  <section ref="matrixRef" class="section-container mt-14 md:mt-20 scroll-mt-20">
    <div class="cyber-label mb-3">// PRODUCTS</div>
    <h2 v-reveal class="cyber-title text-xl md:text-2xl font-bold text-cyber-text">
      {{ t('portal.matrixTitle') }}
    </h2>

    <div class="mt-8 grid gap-5 md:grid-cols-3">
      <ProductCard
        v-for="(card, i) in cards"
        :key="card.key"
        v-reveal="i * 120"
        :logo="card.logo"
        :name="card.name"
        :tagline="card.tagline"
        :points="card.points"
        :accent="card.accent"
        :to="card.path"
        :view-details="t('portal.viewDetails')"
      />
    </div>
  </section>

  <!-- 产品亮点带 -->
  <section class="section-container mt-16 md:mt-24">
    <div class="cyber-label mb-3">// HIGHLIGHTS</div>
    <h2 v-reveal class="cyber-title text-xl md:text-2xl font-bold text-cyber-text">
      {{ t('portal.highlightsTitle') }}
    </h2>

    <div class="mt-10 flex flex-col gap-14 md:gap-20">
      <div
        v-for="(card, i) in cards"
        :key="card.key"
        v-reveal
        class="flex flex-col items-center gap-8 md:gap-14"
        :class="i % 2 === 1 ? 'md:flex-row-reverse' : 'md:flex-row'"
        :style="{ '--pa': card.accent }"
      >
        <!-- 文案 -->
        <div class="w-full md:w-2/5">
          <div class="cyber-label mb-3">// 0{{ i + 1 }}</div>
          <h3 class="font-tech text-2xl font-bold tracking-[0.1em] text-cyber-text uppercase">{{ card.name }}</h3>
          <p class="mt-3 text-sm leading-relaxed text-cyber-muted">{{ card.tagline }}</p>

          <ul class="mt-5 flex flex-col gap-2.5">
            <li v-for="(point, pi) in card.points" :key="pi" class="flex items-center gap-2.5 text-sm text-cyber-muted">
              <span class="inline-block h-1.5 w-1.5" :style="{ background: card.accent }"></span>
              <span>{{ point }}</span>
            </li>
          </ul>

          <el-button class="!mt-7" @click="router.push(card.path)">{{ t('portal.viewDetails') }} →</el-button>
        </div>

        <!-- 主视觉 -->
        <div class="w-full md:w-3/5">
          <div class="highlight-shot p-2">
            <img :src="card.shot" :alt="card.name" class="w-full" loading="lazy" />
          </div>
        </div>
      </div>
    </div>
  </section>

  <!-- 底部 CTA -->
  <section v-reveal class="section-container mt-16 md:mt-24">
    <div class="cyber-panel cyber-panel-accent cyber-corners flex flex-col items-center gap-6 px-6 py-12 text-center">
      <div class="cyber-label">// CONTACT</div>
      <h2 class="font-tech text-xl md:text-2xl font-bold tracking-wider text-cyber-text">{{ t('cta.title') }}</h2>

      <div class="flex flex-wrap justify-center gap-4">
        <el-button type="primary" class="!h-11 !px-8" @click="goContactUs">
          {{ t('cta.message') }}
        </el-button>
      </div>
    </div>
  </section>
</template>

<style scoped>
/* 产品大图：矩形边框完全包裹，无切角 */
.highlight-shot {
  border: 1px solid var(--pa, var(--frame));
  background: var(--panel);
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
