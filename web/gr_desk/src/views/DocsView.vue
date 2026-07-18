<script setup lang="ts">
import { useI18n } from 'vue-i18n'

import cnFlag from '@/assets/icon/ic_cn_flag.svg'
import usFlag from '@/assets/icon/ic_us_flag.svg'
import icWindows from '@/assets/icon/ic_windows.svg'
import icLinux from '@/assets/icon/ic_linux.svg'
import icAndroid from '@/assets/icon/ic_android.svg'

const { t } = useI18n()

function goDocsPage() {
  window.open('https://docs.godesk.uk/', '_blank')
}
</script>

<template>
  <!-- 使用文档 -->
  <section v-reveal class="section-container flex flex-col items-center pt-10 md:pt-16">
    <h1 class="cyber-title justify-center text-2xl md:text-3xl font-bold text-cyber-text">{{ t('docs.title') }}</h1>

    <div class="cyber-panel cyber-corners mt-8 flex w-full max-w-sm flex-col items-center gap-5 p-8">
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

  <!-- 支持平台 -->
  <section v-reveal="120" class="section-container mt-14 md:mt-20">
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
