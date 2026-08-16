<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { BulbFilled, BulbOutlined, CloseOutlined, DownOutlined, MenuOutlined } from '@ant-design/icons-vue'
import { navItems } from '../data/content'
import { useLocale } from '../composables/useLocale'
import type { Locale } from '../i18n'
import PixelLogo from './PixelLogo.vue'
import PixelsWord from './PixelsWord.vue'

defineProps<{ isDark: boolean }>()
defineEmits<{ 'toggle-theme': [] }>()

const { t } = useI18n()
const { isZh, setLocale } = useLocale()

function onLangClick({ key }: { key: string | number }) {
  setLocale(key as Locale)
}

const scrolled = ref(false)
const drawerOpen = ref(false)

function onScroll() {
  scrolled.value = window.scrollY > 12
}

onMounted(() => {
  onScroll()
  window.addEventListener('scroll', onScroll, { passive: true })
})
onBeforeUnmount(() => window.removeEventListener('scroll', onScroll))
</script>

<template>
  <header class="site-header" :class="{ scrolled }">
    <div class="container header-inner">
      <a class="brand" href="#top">
        <span class="brand-mark">
          <PixelLogo :size="30" />
        </span>
        <PixelsWord :height="15" class="brand-word" />
      </a>

      <nav class="nav" aria-label="主导航">
        <a v-for="n in navItems" :key="n.key" class="nav-link" :href="n.href">
          {{ t(`nav.${n.key}`) }}
        </a>
      </nav>

      <div class="header-actions">
        <a-dropdown placement="bottomRight">
          <a-button
            type="text"
            class="lang-btn px-mono"
            :aria-label="t('lang.label')"
          >
            {{ isZh ? '中' : 'EN' }}
            <DownOutlined />
          </a-button>
          <template #overlay>
            <a-menu @click="onLangClick">
              <a-menu-item key="zh-CN">简体中文</a-menu-item>
              <a-menu-item key="en-US">English</a-menu-item>
            </a-menu>
          </template>
        </a-dropdown>
        <a-button
          type="text"
          class="theme-btn"
          :aria-label="isDark ? t('theme.switchToLight') : t('theme.switchToDark')"
          @click="$emit('toggle-theme')"
        >
          <BulbOutlined v-if="!isDark" />
          <BulbFilled v-else />
        </a-button>
        <a-button type="primary" class="px-shadow-btn" href="#contact">{{ t('nav.contact') }}</a-button>
        <a-button class="menu-btn" type="text" aria-label="打开菜单" @click="drawerOpen = true">
          <MenuOutlined />
        </a-button>
      </div>
    </div>

    <a-drawer v-model:open="drawerOpen" placement="right" :width="280" :closable="false">
      <div class="drawer-head">
        <span class="drawer-brand">
          <PixelLogo :size="26" />
          <PixelsWord :height="13" />
        </span>
        <a-button type="text" aria-label="关闭菜单" @click="drawerOpen = false">
          <CloseOutlined />
        </a-button>
      </div>
      <div class="drawer-nav">
        <a
          v-for="n in navItems"
          :key="n.key"
          class="drawer-link"
          :href="n.href"
          @click="drawerOpen = false"
        >
          {{ t(`nav.${n.key}`) }}
        </a>
        <a-button
          type="primary"
          block
          class="px-shadow-btn drawer-cta"
          href="#contact"
          @click="drawerOpen = false"
        >
          {{ t('nav.contact') }}
        </a-button>
      </div>
    </a-drawer>
  </header>
</template>

<style scoped>
.site-header {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  z-index: 100;
  height: 68px;
  display: flex;
  align-items: center;
  background: var(--px-header-bg);
  backdrop-filter: blur(10px);
  border-bottom: 1px solid transparent;
  transition: background 0.25s ease, border-color 0.25s ease, box-shadow 0.25s ease;
}

.site-header.scrolled {
  background: var(--px-header-bg-solid);
  border-bottom-color: var(--px-line);
  box-shadow: 0 2px 12px rgba(22, 37, 29, 0.05);
}

.header-inner {
  display: flex;
  align-items: center;
  justify-content: space-between;
  width: 100%;
}

.brand {
  display: flex;
  align-items: center;
  gap: 12px;
}

.brand-mark {
  color: var(--px-green);
  line-height: 0;
}

.brand-word {
  color: var(--px-ink);
}

.nav {
  display: flex;
  gap: 30px;
}

.nav-link {
  position: relative;
  padding: 6px 0;
  font-size: 15px;
  color: var(--px-gray);
  transition: color 0.2s;
}

.nav-link::after {
  content: '';
  position: absolute;
  left: 0;
  bottom: 0;
  width: 100%;
  height: 2px;
  background: var(--px-green);
  transform: scaleX(0);
  transform-origin: left;
  transition: transform 0.2s;
}

.nav-link:hover {
  color: var(--px-green);
}

.nav-link:hover::after {
  transform: scaleX(1);
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 12px;
}

.theme-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  font-size: 18px;
  color: var(--px-gray);
}

.theme-btn:hover {
  color: var(--px-green);
}

.lang-btn {
  font-size: 13px;
  letter-spacing: 0.5px;
  color: var(--px-gray);
}

.lang-btn:hover {
  color: var(--px-green);
}

.menu-btn {
  display: none;
}

.drawer-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 16px;
}

.drawer-brand {
  display: flex;
  align-items: center;
  gap: 10px;
  color: var(--px-green);
}

.drawer-nav {
  display: flex;
  flex-direction: column;
}

.drawer-link {
  padding: 14px 4px;
  font-size: 15px;
  color: var(--px-ink);
  border-bottom: 1px solid var(--px-line);
}

.drawer-link:hover {
  color: var(--px-green);
}

.drawer-cta {
  margin-top: 20px;
}

@media (max-width: 960px) {
  .nav {
    display: none;
  }

  .menu-btn {
    display: inline-flex;
  }
}
</style>
