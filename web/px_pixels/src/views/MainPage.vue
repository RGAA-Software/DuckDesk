<script setup lang="ts">
import { onMounted, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'
import {
    IconArrowUpRight,
    IconMenu2,
    IconMoon,
    IconSun,
    IconX,
} from '@tabler/icons-vue'
import ContactUs from '@/components/ContactUs.vue'
import pixelsLogo from '@/assets/pixels-logo-45.svg'

type PixelsTheme = 'light' | 'dark'

const { locale, t } = useI18n()
const router = useRouter()
const contactVisible = ref(false)
const menuVisible = ref(false)
const theme = ref<PixelsTheme>('light')

function applyTheme(nextTheme: PixelsTheme) {
    theme.value = nextTheme
    document.documentElement.dataset.theme = nextTheme
    document.documentElement.style.colorScheme = nextTheme
    localStorage.setItem('pixels-theme', nextTheme)
}

function toggleTheme() {
    applyTheme(theme.value === 'light' ? 'dark' : 'light')
}

function toggleLanguage() {
    locale.value = locale.value === 'zh' ? 'en' : 'zh'
    document.documentElement.lang = locale.value === 'zh' ? 'zh-CN' : 'en'
    localStorage.setItem('language', locale.value)
}

function navigateTo(hash = '') {
    menuVisible.value = false
    void router.push({ path: '/main', hash })
}

function navigateToDownloads() {
    menuVisible.value = false
    void router.push('/downloads')
}

function navigateToDocs() {
    menuVisible.value = false
    void router.push('/docs')
}

function openContact() {
    menuVisible.value = false
    contactVisible.value = true
}

watch(
    () => router.currentRoute.value.fullPath,
    () => {
        menuVisible.value = false
    },
)

onMounted(() => {
    applyTheme(localStorage.getItem('pixels-theme') === 'dark' ? 'dark' : 'light')
    document.documentElement.lang = locale.value === 'zh' ? 'zh-CN' : 'en'
})
</script>

<template>
  <div class="site-frame">
    <header class="site-header">
      <div class="site-shell header-inner">
        <button class="brand" type="button" aria-label="PIXELS" @click="navigateTo()">
          <img :src="pixelsLogo" alt="">
          <span>PIXELS</span>
        </button>

        <nav class="desktop-nav" :aria-label="t('nav.home')">
          <button type="button" @click="navigateTo()">{{ t('nav.home') }}</button>
          <button type="button" @click="navigateTo('#solutions')">
            {{ t('site.nav.solutions') }}
          </button>
          <button type="button" @click="navigateTo('#capabilities')">
            {{ t('site.capabilities.eyebrow') }}
          </button>
          <button type="button" @click="navigateTo('#platform')">
            {{ t('site.nav.platform') }}
          </button>
          <button type="button" @click="navigateToDownloads">
            {{ t('site.nav.downloads') }}
          </button>
          <button type="button" @click="navigateToDocs">
            {{ t('site.nav.docs') }}
          </button>
        </nav>

        <div class="header-actions">
          <button
            class="icon-button"
            type="button"
            :aria-label="theme === 'light' ? 'Dark theme' : 'Light theme'"
            @click="toggleTheme"
          >
            <IconMoon v-if="theme === 'light'" :size="17" :stroke-width="1.8" />
            <IconSun v-else :size="17" :stroke-width="1.8" />
          </button>
          <button class="language-button" type="button" @click="toggleLanguage">
            {{ locale === 'zh' ? 'EN' : '中' }}
          </button>
          <button class="header-cta" type="button" @click="openContact">
            {{ t('site.actions.consult') }}
            <IconArrowUpRight :size="16" :stroke-width="1.9" aria-hidden="true" />
          </button>
        </div>

        <button
          class="menu-button"
          type="button"
          :aria-expanded="menuVisible"
          aria-label="Menu"
          @click="menuVisible = !menuVisible"
        >
          <IconX v-if="menuVisible" :size="19" :stroke-width="1.8" />
          <IconMenu2 v-else :size="19" :stroke-width="1.8" />
        </button>
      </div>

      <div v-if="menuVisible" class="mobile-panel">
        <nav class="mobile-nav">
          <button type="button" @click="navigateTo()">{{ t('nav.home') }}</button>
          <button type="button" @click="navigateTo('#solutions')">
            {{ t('site.nav.solutions') }}
          </button>
          <button type="button" @click="navigateTo('#capabilities')">
            {{ t('site.capabilities.eyebrow') }}
          </button>
          <button type="button" @click="navigateTo('#platform')">
            {{ t('site.nav.platform') }}
          </button>
          <button type="button" @click="navigateToDownloads">
            {{ t('site.nav.downloads') }}
          </button>
          <button type="button" @click="navigateToDocs">
            {{ t('site.nav.docs') }}
          </button>
        </nav>
        <div class="mobile-tools">
          <button type="button" @click="toggleTheme">
            {{ theme === 'light' ? 'Dark' : 'Light' }}
          </button>
          <button type="button" @click="toggleLanguage">
            {{ locale === 'zh' ? 'English' : '中文' }}
          </button>
          <button class="mobile-contact" type="button" @click="openContact">
            {{ t('site.actions.consult') }}
            <IconArrowUpRight :size="15" :stroke-width="1.9" />
          </button>
        </div>
      </div>
    </header>

    <main>
      <RouterView />
    </main>

    <footer class="site-footer">
      <div class="site-shell footer-grid">
        <div class="footer-brand">
          <div class="brand">
            <img :src="pixelsLogo" alt="">
            <span>PIXELS</span>
          </div>
          <p>{{ t('site.footer.description') }}</p>
          <strong>{{ t('site.footer.tagline') }}</strong>
        </div>
        <div class="footer-column">
          <span>{{ t('site.nav.solutions') }}</span>
          <button type="button" @click="navigateTo('#solutions')">
            {{ t('site.solutions.remote.title') }}
          </button>
          <button type="button" @click="navigateTo('#solutions')">
            {{ t('site.solutions.game.title') }}
          </button>
          <button type="button" @click="navigateTo('#solutions')">
            {{ t('site.solutions.render.title') }}
          </button>
        </div>
        <div class="footer-column">
          <span>{{ t('site.footer.support') }}</span>
          <button type="button" @click="navigateTo('#platform')">
            {{ t('site.nav.platform') }}
          </button>
          <button type="button" @click="navigateToDownloads">
            {{ t('site.nav.downloads') }}
          </button>
          <button type="button" @click="navigateToDocs">
            {{ t('site.nav.docs') }}
          </button>
          <button type="button" @click="openContact">
            {{ t('site.nav.support') }}
          </button>
          <button type="button">{{ t('site.footer.legal') }}</button>
        </div>
      </div>
      <div class="site-shell footer-bottom">
        <small>{{ t('site.footer.copyright') }}</small>
        <small>REMOTE · PLAY · RENDER</small>
      </div>
    </footer>

    <ContactUs v-model="contactVisible" />
  </div>
</template>

<style scoped>
.site-frame {
    --background: #fafafa;
    --foreground: #18181b;
    --card: #ffffff;
    --primary: #007f49;
    --primary-foreground: #ffffff;
    --secondary: #f4f4f5;
    --secondary-foreground: #27272a;
    --muted-foreground: #71717a;
    --accent: #ecfdf5;
    --accent-foreground: #006b3d;
    --border: #e4e4e7;
    --ring: #009a59;
    min-height: 100vh;
    background: var(--background);
    color: var(--foreground);
}

.site-shell {
    width: min(1200px, calc(100% - 48px));
    margin: 0 auto;
}

.site-header {
    position: sticky;
    top: 0;
    z-index: 50;
    border-bottom: 1px solid color-mix(in srgb, var(--border) 80%, transparent);
    background: color-mix(in srgb, var(--background) 88%, transparent);
    backdrop-filter: blur(18px) saturate(150%);
}

.header-inner {
    display: flex;
    align-items: center;
    height: 72px;
    min-height: 72px;
}

.header-inner > .brand,
.header-inner > .desktop-nav,
.header-inner > .header-actions {
    align-self: stretch;
}

.brand {
    display: inline-flex;
    align-items: center;
    gap: 11px;
    padding: 0;
    border: 0;
    background: transparent;
    color: var(--foreground);
    cursor: pointer;
}

.brand img {
    width: 28px;
    height: 28px;
}

.brand span {
    display: inline-flex;
    align-items: center;
    font-family: "10 Pixel", sans-serif;
    font-size: 22px;
    font-weight: 700;
    letter-spacing: 0.08em;
    line-height: 1;
}

.desktop-nav {
    display: flex;
    align-items: center;
    gap: 30px;
    margin-left: 58px;
}

.desktop-nav button,
.footer-column button {
    padding: 0;
    border: 0;
    background: transparent;
    color: var(--muted-foreground);
    cursor: pointer;
    font: 500 14px var(--font-ui);
    transition: color 160ms ease;
}

.desktop-nav button {
    display: inline-flex;
    height: 38px;
    align-items: center;
    line-height: 1;
}

.desktop-nav button:hover,
.footer-column button:hover {
    color: var(--primary);
}

.header-actions {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-left: auto;
}

.icon-button,
.language-button {
    display: grid;
    width: 38px;
    height: 38px;
    place-items: center;
    border: 1px solid var(--border);
    border-radius: 10px !important;
    background: var(--card);
    color: var(--secondary-foreground);
    cursor: pointer;
}

.icon-button svg {
    width: 17px;
}

.language-button {
    font: 700 12px var(--font-tech);
}

.header-cta {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    min-height: 38px;
    margin-left: 2px;
    padding: 0 16px;
    border: 0;
    border-radius: 10px !important;
    background: var(--primary);
    box-shadow: 0 8px 20px rgba(0, 127, 73, 0.16);
    color: var(--primary-foreground);
    cursor: pointer;
    font: 700 13px var(--font-ui);
    line-height: 1;
}

.menu-button,
.mobile-panel {
    display: none;
}

.site-footer {
    border-top: 1px solid var(--border);
    background: var(--card);
}

.footer-grid {
    display: grid;
    grid-template-columns: minmax(320px, 1.8fr) 1fr 1fr;
    gap: 72px;
    padding: 68px 0 54px;
}

.footer-brand p {
    max-width: 420px;
    margin: 22px 0 12px;
    color: var(--muted-foreground);
    font-size: 14px;
    line-height: 1.75;
}

.footer-brand strong {
    color: var(--accent-foreground);
    font-size: 13px;
    font-weight: 650;
}

.footer-column {
    display: flex;
    align-items: flex-start;
    flex-direction: column;
    gap: 14px;
}

.footer-column > span {
    margin-bottom: 5px;
    color: var(--foreground);
    font-size: 13px;
    font-weight: 700;
}

.footer-bottom {
    display: flex;
    justify-content: space-between;
    padding: 20px 0 26px;
    border-top: 1px solid var(--border);
    color: var(--muted-foreground);
    font: 11px var(--font-tech);
    letter-spacing: 0.05em;
}

:global(html[data-theme='dark'] .site-frame) {
    --background: #09090b;
    --foreground: #fafafa;
    --card: #101014;
    --primary: #009a59;
    --primary-foreground: #ffffff;
    --secondary: #27272a;
    --secondary-foreground: #fafafa;
    --muted-foreground: #a1a1aa;
    --accent: #052e22;
    --accent-foreground: #8ceec0;
    --border: #27272a;
    --ring: #8ceec0;
}

:global(html[data-theme='light'] body) {
    background: #fafafa;
}

:global(html[data-theme='dark'] body) {
    background: #09090b;
}

@media (max-width: 1080px) {
    .site-shell {
        width: min(100% - 32px, 680px);
    }

    .header-inner {
        min-height: 66px;
    }

    .desktop-nav,
    .header-actions {
        display: none;
    }

    .menu-button {
        display: grid;
        width: 40px;
        height: 40px;
        place-content: center;
        margin-left: auto;
        border: 1px solid var(--border);
        border-radius: 10px !important;
        background: var(--card);
    }

    .mobile-panel {
        display: block;
        padding: 8px 16px 18px;
        border-top: 1px solid var(--border);
        background: var(--background);
    }

    .mobile-nav {
        display: grid;
    }

    .mobile-nav button {
        padding: 15px 0;
        border: 0;
        border-bottom: 1px solid var(--border);
        background: transparent;
        color: var(--foreground);
        text-align: left;
        font: 600 15px var(--font-ui);
    }

    .mobile-tools {
        display: grid;
        grid-template-columns: 1fr 1fr 2fr;
        gap: 8px;
        padding-top: 14px;
    }

    .mobile-tools button {
        display: inline-flex;
        align-items: center;
        justify-content: center;
        gap: 6px;
        padding: 11px 8px;
        border: 1px solid var(--border);
        border-radius: 9px !important;
        background: var(--card);
        color: var(--foreground);
        font: 600 12px var(--font-ui);
    }

    .mobile-tools .mobile-contact {
        border-color: var(--primary);
        background: var(--primary);
        color: var(--primary-foreground);
    }

    .footer-grid {
        grid-template-columns: 1fr 1fr;
        gap: 42px 28px;
        padding: 50px 0 38px;
    }

    .footer-brand {
        grid-column: 1 / -1;
    }
}

@media (max-width: 480px) {
    .footer-grid {
        grid-template-columns: 1fr;
    }

    .footer-brand {
        grid-column: auto;
    }

    .footer-bottom {
        gap: 12px;
        flex-direction: column;
    }
}
</style>
