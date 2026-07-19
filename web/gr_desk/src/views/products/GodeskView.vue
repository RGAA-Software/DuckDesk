<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'

import downloadIcon from '@/assets/icon/ic_download.svg'
import writeIcon from '@/assets/icon/ic_write.svg'
import cnFlag from '@/assets/icon/ic_cn_flag.svg'
import usFlag from '@/assets/icon/ic_us_flag.svg'
import iconLogo from '@/assets/icon/ic_trans_icon_blue.png'

import preview1 from '@/assets/preview/shot_1.png'
import preview2 from '@/assets/preview/shot_2.png'
import preview3 from '@/assets/preview/shot_3.png'
import preview4 from '@/assets/preview/shot_5.png'

import webInfoMain from '@/assets/main/ic_new_experience.svg'
import webInfoFileTransfer from '@/assets/main/ic_file_transfer.svg'
import webInfoSecurity from '@/assets/main/ic_private.svg'

import resGame from '@/assets/main/res-game.jpg'
import resFinancial from '@/assets/main/res-financial.jpg'
import resEnergy from '@/assets/main/res-energy.jpg'
import resArchitecture from '@/assets/main/res-architecture.jpg'
import resMedical from '@/assets/main/res-medical.jpg'
import resCatering from '@/assets/main/res-catering.jpg'
import resEducation from '@/assets/main/res-education.jpg'
import resSupport from '@/assets/main/res-support.jpg'

import ContactUs from '@/components/ContactUs.vue'
import ProductHero from '@/components/ProductHero.vue'

const { t, tm } = useI18n()
const router = useRouter()

// 截图轮播
const imageList = [preview1, preview2, preview3, preview4]

// 轮播容器按 16:9 计算高度（el-carousel 需要显式高度）
const carouselWrapRef = ref<HTMLElement>()
const carouselHeight = ref('405px')
let carouselObserver: ResizeObserver | null = null

onMounted(() => {
  if (!carouselWrapRef.value) return
  carouselObserver = new ResizeObserver((entries) => {
    const width = entries[0]?.contentRect.width ?? 0
    if (width > 0) {
      carouselHeight.value = `${(width * 9) / 16}px`
    }
  })
  carouselObserver.observe(carouselWrapRef.value)
})

onBeforeUnmount(() => {
  carouselObserver?.disconnect()
})

// 行业方案配图（顺序与 locales industries.items 对应）
const industryImages = [
  resGame,
  resEducation,
  resFinancial,
  resEnergy,
  resArchitecture,
  resMedical,
  resSupport,
  resCatering,
]

interface FeatureItem {
  title: string
  desc: string
}

const featureItems = (key: string): FeatureItem[] => tm(key) as FeatureItem[]

const sections = computed(() => [
  { key: 'experience', title: t('features.experience.title'), image: webInfoMain },
  { key: 'fileTransfer', title: t('features.fileTransfer.title'), image: webInfoFileTransfer },
  { key: 'security', title: t('features.security.title'), image: webInfoSecurity },
])

const kpis = computed(() => tm('hero.kpis') as Array<{ num: string; label: string }>)

const industryItems = computed(() => {
  const names = tm('industries.items') as string[]
  return names.map((name, i) => ({ name, image: industryImages[i] }))
})

function downloadKuaKe() {
  window.open('https://pan.quark.cn/s/bfe11452992b', '_blank')
}

function downloadGithub() {
  window.open('https://github.com/RGAA-Software/GammaRay/releases', '_blank')
}

const contactUsVisible = ref(false)
const goContactUs = () => {
  contactUsVisible.value = true
}
</script>

<template>
  <div style="--pa: #2f8fff">
    <ContactUs v-model="contactUsVisible" />

    <!-- 产品 Hero -->
    <ProductHero
      :logo="iconLogo"
      :name="t('productNames.godesk')"
      :status="t('products.godesk.status')"
      :tagline="t('products.godesk.tagline')"
    >
      <el-popover placement="bottom" :width="235" trigger="click">
        <template #reference>
          <el-button type="primary" class="!h-11 !px-8">
            <el-image :src="downloadIcon" fit="cover" class="h-5 w-5" />
            <span class="ml-1">{{ t('products.common.download') }}</span>
          </el-button>
        </template>

        <div class="flex w-full flex-col gap-3 py-2">
          <div class="flex items-center gap-3">
            <el-image :src="cnFlag" fit="cover" class="h-8 w-8" />
            <el-button @click="downloadKuaKe">{{ t('hero.downloadLine1') }}</el-button>
          </div>
          <div class="flex items-center gap-3">
            <el-image :src="usFlag" fit="cover" class="h-8 w-8" />
            <el-button @click="downloadGithub">{{ t('hero.downloadLine2') }}</el-button>
          </div>
        </div>
      </el-popover>

      <el-button class="!h-11 !px-8" @click="router.push('/price')">{{ t('products.common.pricing') }}</el-button>
      <el-button class="!h-11 !px-8" @click="router.push('/docs')">{{ t('products.common.docs') }}</el-button>
    </ProductHero>

    <!-- KPI 数据条 -->
    <section v-reveal class="section-container mt-4">
      <div class="cyber-panel grid grid-cols-2 gap-6 p-6 md:grid-cols-4 md:p-8">
        <div v-for="kpi in kpis" :key="kpi.label" class="flex flex-col gap-2">
          <div class="kpi-num">{{ kpi.num }}</div>
          <div class="font-tech text-[11px] tracking-[0.14em] text-cyber-muted uppercase">{{ kpi.label }}</div>
        </div>
      </div>
    </section>

    <!-- 产品截图轮播 -->
    <section v-reveal class="section-container mt-14 md:mt-20">
      <div class="cyber-label mb-4 text-center">// SCREENSHOTS</div>
      <div ref="carouselWrapRef" class="cyber-corners mx-auto w-full max-w-6xl">
        <el-carousel :interval="5000" arrow="always" :height="carouselHeight">
          <el-carousel-item v-for="(image, index) in imageList" :key="index">
            <div class="h-full w-full overflow-hidden border border-cyber-frame">
              <el-image :src="image" class="h-full w-full" fit="cover" />
            </div>
          </el-carousel-item>
        </el-carousel>
      </div>
    </section>

    <!-- 卖点卡片矩阵（统一图标尺寸与卡片高度，自动对齐） -->
    <section v-reveal class="section-container mt-14 md:mt-20">
      <div class="cyber-label mb-4 text-center">// FEATURES</div>
      <div class="grid items-stretch gap-5 sm:grid-cols-2 lg:grid-cols-3">
        <div
          v-for="(section, si) in sections"
          :key="section.key"
          v-reveal="(si % 3) * 100"
          class="feature-card cyber-panel flex flex-col p-6"
        >
          <div class="flex items-center gap-4">
            <div class="feature-icon">
              <img :src="section.image" :alt="section.title" class="h-9 w-9 object-contain" />
            </div>
            <h3 class="font-tech text-base font-bold tracking-wider text-cyber-text">{{ section.title }}</h3>
          </div>

          <ul class="mt-5 flex flex-col gap-4">
            <li v-for="(item, i) in featureItems(`features.${section.key}.items`)" :key="i">
              <div class="flex items-center gap-2.5">
                <span class="cyber-dot"></span>
                <span class="font-tech text-sm font-bold tracking-wider text-cyber-text">{{ item.title }}</span>
              </div>
              <p class="mt-1 pl-4.5 text-xs leading-relaxed text-cyber-muted">{{ item.desc }}</p>
            </li>
          </ul>
        </div>
      </div>
    </section>

    <!-- 行业解决方案 -->
    <section class="section-container mt-6 md:mt-10">
      <h2 v-reveal class="cyber-title justify-center text-xl md:text-2xl font-bold text-cyber-text">
        {{ t('industries.title') }}
      </h2>

      <div class="mt-8 grid grid-cols-2 gap-4 md:grid-cols-4 md:gap-5">
        <div
          v-for="(item, index) in industryItems"
          :key="item.name"
          v-reveal="(index % 4) * 100"
          class="industry-card group"
        >
          <div class="overflow-hidden">
            <img
              :src="item.image"
              :alt="item.name"
              class="aspect-[23/25] w-full object-cover transition-transform duration-500 group-hover:scale-105"
            />
          </div>
          <div class="border-t border-cyber-line bg-cyber-bg2 py-2.5 text-center">
            <span class="font-tech text-xs md:text-sm tracking-wider text-cyber-text">{{ item.name }}</span>
          </div>
        </div>
      </div>
    </section>

    <!-- 底部 CTA -->
    <section v-reveal class="section-container mt-16 md:mt-24">
      <div class="cyber-panel cyber-panel-accent cyber-corners flex flex-col items-center gap-6 px-6 py-12 text-center">
        <div class="cyber-label">// READY</div>
        <h2 class="font-tech text-xl md:text-2xl font-bold tracking-wider text-cyber-text">{{ t('cta.title') }}</h2>

        <div class="flex flex-wrap justify-center gap-4">
          <el-button type="primary" class="!h-11 !px-8" @click="goContactUs">
            <el-image :src="writeIcon" fit="cover" class="h-5 w-5" />
            <span class="ml-1">{{ t('cta.message') }}</span>
          </el-button>

        </div>
      </div>
    </section>
  </div>
</template>

<style scoped>
/* 卖点卡片：统一图标盒与悬停效果 */
.feature-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 56px;
  height: 56px;
  flex-shrink: 0;
  border: 1px solid var(--frame);
  background: var(--bg2, rgba(255, 255, 255, 0.03));
}
.feature-card {
  transition: transform 0.2s;
}
.feature-card:hover {
  transform: translateY(-3px);
}
.feature-card:hover::before {
  background: var(--brand-dark);
}

/* 行业卡片：切角边框 */
.industry-card {
  --cut: 9px;
  background: var(--panel);
  clip-path: polygon(
    var(--cut) 0,
    100% 0,
    100% calc(100% - var(--cut)),
    calc(100% - var(--cut)) 100%,
    0 100%,
    0 var(--cut)
  );
  position: relative;
  isolation: isolate;
}
.industry-card::before {
  content: '';
  position: absolute;
  inset: 0;
  background: var(--frame);
  clip-path: inherit;
  z-index: -2;
  transition: background 0.2s;
}
.industry-card::after {
  content: '';
  position: absolute;
  inset: 1px;
  background: var(--panel);
  clip-path: inherit;
  z-index: -1;
}
.industry-card:hover::before {
  background: var(--brand-dark);
}
</style>
