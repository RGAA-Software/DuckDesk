<script setup lang="ts">
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'

import checkIcon from '@/assets/icon/ic_check.svg'
import warnIcon from '@/assets/icon/ic_warn.svg'
import writeIcon from '@/assets/icon/ic_write.svg'
import emailIcon from '@/assets/icon/ic_email.svg'
import downloadIcon from '@/assets/icon/ic_download.svg'
import cnFlag from '@/assets/icon/ic_cn_flag.svg'
import usFlag from '@/assets/icon/ic_us_flag.svg'
import ContactUs from '@/components/ContactUs.vue'

const { t, tm } = useI18n()

const personalFeatures = computed(() => tm('price.personal.features') as string[])
const enterpriseFeatures = computed(() => tm('price.enterprise.features') as string[])

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
  <!-- 标题区 -->
  <section v-reveal class="section-container flex flex-col items-center pt-10 text-center md:pt-16">
    <h1 class="cyber-title justify-center text-3xl md:text-5xl font-bold text-cyber-text">
      {{ t('price.titleSelf') }}{{ t('price.titleSolution') }}
    </h1>

    <p class="mt-4 font-tech text-sm tracking-[0.14em] text-cyber-muted uppercase">{{ t('price.subtitle') }}</p>

    <div class="mt-6 flex items-center gap-2 border border-cyber-amber/60 bg-cyber-amber/10 px-5 py-2.5">
      <img :src="warnIcon" alt="" class="h-5 w-5" />
      <span class="font-tech text-sm md:text-base font-bold tracking-wider text-cyber-amber">{{ t('price.warn') }}</span>
    </div>
  </section>

  <!-- 价格卡片 -->
  <section class="section-container mt-10 md:mt-14">
    <div class="mx-auto grid max-w-4xl gap-6 md:grid-cols-2 md:gap-8">
      <!-- 个人版 -->
      <div v-reveal class="cyber-panel flex flex-col p-6 md:p-8">
        <div class="cyber-label">// PERSONAL</div>
        <h3 class="mt-2 font-tech text-lg font-bold tracking-wider text-cyber-text">{{ t('price.personal.name') }}</h3>
        <p class="kpi-num mt-3 !text-cyber-green">{{ t('price.personal.price') }}</p>

        <div class="kpi-line mt-4"><i style="width: 100%"></i></div>

        <ul class="mt-6 grid gap-3 sm:grid-cols-2 md:grid-cols-1 lg:grid-cols-2">
          <li
            v-for="(desc, index) in personalFeatures"
            :key="index"
            class="flex items-center gap-2 text-sm text-cyber-muted"
          >
            <img :src="checkIcon" alt="" class="h-4.5 w-4.5 shrink-0" />
            <span>{{ desc }}</span>
          </li>
        </ul>
      </div>

      <!-- 企业版 -->
      <div v-reveal="120" class="cyber-panel cyber-panel-green cyber-corners relative flex flex-col p-6 md:p-8">
        <span class="cyber-tag absolute -top-0 right-6">{{ t('price.enterprise.recommended') }}</span>

        <div class="cyber-label">// ENTERPRISE</div>
        <h3 class="mt-2 font-tech text-lg font-bold tracking-wider text-cyber-text">{{ t('price.enterprise.name') }}</h3>
        <p class="kpi-num mt-3 !text-cyber-green">{{ t('price.enterprise.price') }}</p>

        <div class="kpi-line mt-4"><i style="width: 100%"></i></div>

        <ul class="mt-6 flex flex-col gap-3">
          <li
            v-for="(desc, index) in enterpriseFeatures"
            :key="index"
            class="flex items-center gap-2 text-sm text-cyber-muted"
          >
            <img :src="checkIcon" alt="" class="h-4.5 w-4.5 shrink-0" />
            <span>{{ desc }}</span>
          </li>
        </ul>

        <p class="mt-4 font-tech text-base font-bold tracking-wider text-cyber-text">
          {{ t('price.enterprise.allUnlocked') }}
        </p>
      </div>
    </div>
  </section>

  <!-- 底部操作按钮 -->
  <section v-reveal class="section-container mt-14 md:mt-20">
    <div class="flex flex-wrap justify-center gap-4">
      <el-popover placement="top" :width="235" trigger="click">
        <template #reference>
          <el-button type="primary" class="!h-11 !px-8">
            <el-image :src="downloadIcon" fit="cover" class="h-5 w-5" />
            <span class="ml-1">{{ t('hero.download') }}</span>
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

      <el-button type="primary" class="!h-11 !px-8" plain @click="goContactUs">
        <el-image :src="writeIcon" fit="cover" class="h-5 w-5" />
        <span class="ml-1">{{ t('cta.message') }}</span>
      </el-button>

      <el-popover placement="top" :width="265" trigger="click">
        <template #reference>
          <el-button class="!h-11 !px-8">
            <el-image :src="emailIcon" fit="cover" class="h-5 w-5" />
            <span class="ml-1">{{ t('cta.email') }}</span>
          </el-button>
        </template>

        <div class="flex h-10 w-full items-center justify-center">
          <span class="font-tech text-base font-bold">{{ t('cta.emailAddress') }}</span>
        </div>
      </el-popover>
    </div>
  </section>

  <ContactUs v-model="contactUsVisible" />
</template>
