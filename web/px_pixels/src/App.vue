<script setup lang="ts">
import { computed, watchEffect } from 'vue'
import { theme as antdTheme } from 'ant-design-vue'
import zhCN from 'ant-design-vue/es/locale/zh_CN'
import enUS from 'ant-design-vue/es/locale/en_US'
import { useI18n } from 'vue-i18n'
import { useTheme } from './composables/useTheme'
import { useLocale } from './composables/useLocale'
import SiteHeader from './components/SiteHeader.vue'
import Hero from './components/Hero.vue'
import Services from './components/Services.vue'
import Industries from './components/Industries.vue'
import Cooperation from './components/Cooperation.vue'
import Advantages from './components/Advantages.vue'
import Process from './components/Process.vue'
import Packages from './components/Packages.vue'
import Faq from './components/Faq.vue'
import Contact from './components/Contact.vue'
import SiteFooter from './components/SiteFooter.vue'

/* Ant Design Vue 全局主题：主题色 #00b96b，直角圆角营造像素感，随暗色模式切换算法 */
const { isDark, toggle } = useTheme()
const { locale } = useLocale()
const { t } = useI18n()

const theme = computed(() => ({
  token: {
    colorPrimary: '#00b96b',
    colorLink: '#00b96b',
    borderRadius: 2,
  },
  algorithm: isDark.value ? antdTheme.darkAlgorithm : antdTheme.defaultAlgorithm,
}))

/* Ant Design 组件库语言（表单校验提示等） */
const antdLocale = computed(() => (locale.value === 'zh-CN' ? zhCN : enUS))

/* 页面标题与描述随语言切换 */
watchEffect(() => {
  document.title = t('meta.title')
  document
    .querySelector('meta[name="description"]')
    ?.setAttribute('content', t('meta.description'))
})
</script>

<template>
  <a-config-provider :theme="theme" :locale="antdLocale">
    <a-app>
      <SiteHeader :is-dark="isDark" @toggle-theme="toggle" />
      <main>
        <Hero />
        <Services />
        <Industries />
        <Cooperation />
        <Advantages />
        <Process />
        <Packages />
        <Faq />
        <Contact />
      </main>
      <SiteFooter />
    </a-app>
  </a-config-provider>
</template>
