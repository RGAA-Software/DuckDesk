<script setup lang="ts">
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'

import cnFlag from '@/assets/icon/ic_cn_flag.svg'
import usFlag from '@/assets/icon/ic_us_flag.svg'
import icWindows from '@/assets/icon/ic_windows.svg'
import icLinux from '@/assets/icon/ic_linux.svg'
import icAndroid from '@/assets/icon/ic_android.svg'
import githubIcon from '@/assets/ic_github.svg'
import ContactUs from '@/components/ContactUs.vue'

const { t, tm } = useI18n()

// ---------- 产品分页 ----------
const activeTab = ref<'godesk' | 'goxr' | 'cybermonitor'>('godesk')
const tabs = computed(() => [
  { key: 'godesk' as const, label: t('price.tabs.godesk'), accent: '#2f8fff' },
  { key: 'goxr' as const, label: t('price.tabs.goxr'), accent: '#2ac7c4' },
  { key: 'cybermonitor' as const, label: t('price.tabs.cybermonitor'), accent: '#7548d8' },
])

interface PlatformGroup {
  name: string
  systems: string[]
}

const goxrPlatforms = computed(() => tm('docs.goxrPlatforms') as PlatformGroup[])
const cmonPlatforms = computed(() => tm('docs.cmonPlatforms') as PlatformGroup[])

function goDocsPage() {
  window.open('https://docs.godesk.uk/', '_blank')
}

function goCyberGithub() {
  window.open('https://github.com/RGAA-Software/CyberDesktop', '_blank')
}

const contactUsVisible = ref(false)
const goContactUs = () => {
  contactUsVisible.value = true
}
</script>

<template>
  <!-- 标题 + 产品分页 -->
  <section v-reveal class="section-container flex flex-col items-center pt-10 md:pt-16">
    <h1 class="cyber-title justify-center text-2xl md:text-3xl font-bold text-cyber-text">{{ t('docs.title') }}</h1>

    <div class="mt-8 flex flex-wrap justify-center gap-3">
      <button
        v-for="tab in tabs"
        :key="tab.key"
        class="cyber-tab"
        :class="{ 'cyber-tab-active': activeTab === tab.key }"
        :style="{ '--ta': tab.accent }"
        @click="activeTab = tab.key"
      >
        {{ tab.label }}
      </button>
    </div>
  </section>

  <!-- ============ GoDesk ============ -->
  <template v-if="activeTab === 'godesk'">
    <section v-reveal class="section-container flex flex-col items-center pt-10">
      <div class="cyber-panel cyber-corners flex w-full max-w-sm flex-col items-center gap-5 p-8">
        <div class="flex items-center gap-2 font-tech text-base font-bold tracking-wider text-cyber-text">
          <img :src="cnFlag" alt="" class="h-8 w-8" />
          <img :src="usFlag" alt="" class="h-8 w-8" />
          <span>{{ t('docs.officialSite') }}</span>
        </div>
        <el-button type="primary" class="!h-10 !px-10" @click="goDocsPage">
          {{ t('docs.visit') }}
        </el-button>
      </div>
    </section>

    <section v-reveal="120" class="section-container mt-12 md:mt-16">
      <h2 class="cyber-title justify-center text-xl md:text-2xl font-bold text-cyber-text">{{ t('docs.platforms') }}</h2>

      <div class="mx-auto mt-8 grid max-w-2xl gap-6 sm:grid-cols-2">
        <div class="cyber-panel p-6 md:p-8">
          <div class="cyber-label mb-2">// CLIENT</div>
          <h3 class="font-tech text-lg font-bold tracking-wider text-cyber-text">{{ t('docs.client') }}</h3>

          <div class="mt-5 flex flex-col gap-5">
            <div class="flex items-center gap-3 text-base text-cyber-muted">
              <img :src="icWindows" alt="Windows" class="h-8 w-8" />
              Windows
            </div>
            <div class="flex items-center gap-3 text-base text-cyber-muted">
              <img :src="icAndroid" alt="Android" class="h-8 w-8" />
              Android
            </div>
          </div>
        </div>

        <div class="cyber-panel p-6 md:p-8">
          <div class="cyber-label mb-2">// CONSOLE</div>
          <h3 class="font-tech text-lg font-bold tracking-wider text-cyber-text">{{ t('docs.server') }}</h3>

          <div class="mt-5 flex flex-col gap-5">
            <div class="flex items-center gap-3 text-base text-cyber-muted">
              <img :src="icWindows" alt="Windows" class="h-8 w-8" />
              Windows
            </div>
            <div class="flex items-center gap-3 text-base text-cyber-muted">
              <img :src="icLinux" alt="Linux" class="h-8 w-8" />
              Linux
            </div>
          </div>
        </div>
      </div>
    </section>
  </template>

  <!-- ============ GoXR ============ -->
  <template v-if="activeTab === 'goxr'">
    <section v-reveal class="section-container flex flex-col items-center pt-10" style="--pa: #2ac7c4">
      <div class="cyber-panel cyber-corners flex w-full max-w-xl flex-col items-center gap-4 p-8 text-center">
        <p class="text-sm leading-relaxed text-cyber-muted">{{ t('docs.consultDocs') }}</p>
        <el-button type="primary" class="!h-10 !px-10" @click="goContactUs">
          {{ t('products.common.consult') }}
        </el-button>
      </div>
    </section>

    <section v-reveal="120" class="section-container mt-12 md:mt-16" style="--pa: #2ac7c4">
      <h2 class="cyber-title justify-center text-xl md:text-2xl font-bold text-cyber-text">{{ t('docs.platforms') }}</h2>

      <div class="mx-auto mt-8 grid max-w-4xl gap-6 sm:grid-cols-3">
        <div v-for="(group, i) in goxrPlatforms" :key="i" class="cyber-panel p-6">
          <div class="cyber-label mb-2">// 0{{ i + 1 }}</div>
          <h3 class="font-tech text-base font-bold tracking-wider text-cyber-text">{{ group.name }}</h3>
          <div class="mt-4 flex flex-col gap-3">
            <div v-for="(sys, si) in group.systems" :key="si" class="flex items-center gap-2 text-sm text-cyber-muted">
              <span class="cyber-dot"></span>
              <span>{{ sys }}</span>
            </div>
          </div>
        </div>
      </div>
    </section>
  </template>

  <!-- ============ CyberMonitor ============ -->
  <template v-if="activeTab === 'cybermonitor'">
    <section v-reveal class="section-container flex flex-col items-center pt-10" style="--pa: #7548d8">
      <div class="cyber-panel cyber-corners flex w-full max-w-sm flex-col items-center gap-5 p-8">
        <div class="flex items-center gap-2 font-tech text-base font-bold tracking-wider text-cyber-text">
          <span>{{ t('docs.githubDocs') }}</span>
        </div>
        <el-button type="primary" class="!h-10 !px-10" @click="goCyberGithub">
          <img :src="githubIcon" alt="" class="h-5 w-5" />
          <span class="ml-1">{{ t('products.common.github') }}</span>
        </el-button>
      </div>
    </section>

    <section v-reveal="120" class="section-container mt-12 md:mt-16" style="--pa: #7548d8">
      <h2 class="cyber-title justify-center text-xl md:text-2xl font-bold text-cyber-text">{{ t('docs.platforms') }}</h2>

      <div class="mx-auto mt-8 grid max-w-2xl gap-6 sm:grid-cols-2">
        <div v-for="(group, i) in cmonPlatforms" :key="i" class="cyber-panel p-6">
          <div class="cyber-label mb-2">// 0{{ i + 1 }}</div>
          <h3 class="font-tech text-base font-bold tracking-wider text-cyber-text">{{ group.name }}</h3>
          <div class="mt-4 flex flex-col gap-3">
            <div v-for="(sys, si) in group.systems" :key="si" class="flex items-center gap-2 text-sm text-cyber-muted">
              <span class="cyber-dot"></span>
              <span>{{ sys }}</span>
            </div>
          </div>
        </div>
      </div>
    </section>
  </template>

  <ContactUs v-model="contactUsVisible" />
</template>
