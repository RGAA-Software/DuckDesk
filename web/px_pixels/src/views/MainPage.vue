<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch, type Component } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'
import {
    IconArrowUpRight,
    IconChevronDown,
    IconCube3dSphere,
    IconDeviceDesktop,
    IconDeviceGamepad2,
    IconMenu2,
    IconMoon,
    IconSun,
    IconX,
} from '@tabler/icons-vue'
import ContactUs from '@/components/ContactUs.vue'
import pixelsLogo from '@/assets/pixels-logo-45.svg'

type PixelsTheme = 'light' | 'dark'
type SolutionKey = 'remote' | 'gaming' | 'rendering'
type NavigationKey = 'home' | 'solutions' | 'downloads' | 'docs' | 'pricing' | 'about'

interface SolutionMenuItem {
    key: SolutionKey
    path: string
    icon: Component
}

const { locale, t } = useI18n()
const router = useRouter()
const contactVisible = ref(false)
const menuVisible = ref(false)
const solutionsMenuVisible = ref(false)
const theme = ref<PixelsTheme>('light')

const solutionMenuItems: SolutionMenuItem[] = [
    {
        key: 'remote',
        path: '/solutions/remote-desktop',
        icon: IconDeviceDesktop,
    },
    {
        key: 'gaming',
        path: '/solutions/cloud-gaming',
        icon: IconDeviceGamepad2,
    },
    {
        key: 'rendering',
        path: '/solutions/cloud-rendering',
        icon: IconCube3dSphere,
    },
]

const activeNavigation = computed<NavigationKey>(() => {
    const currentPath = router.currentRoute.value.path

    if (currentPath.startsWith('/solutions/')) return 'solutions'
    if (currentPath === '/downloads') return 'downloads'
    if (currentPath === '/docs') return 'docs'
    if (currentPath === '/pricing') return 'pricing'
    if (currentPath === '/about') return 'about'
    return 'home'
})

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
    solutionsMenuVisible.value = false
    void router.push({ path: '/main', hash })
}

function navigateToSolution(solutionPath: string) {
    menuVisible.value = false
    solutionsMenuVisible.value = false
    void router.push(solutionPath)
}

function navigateToDownloads() {
    menuVisible.value = false
    solutionsMenuVisible.value = false
    void router.push('/downloads')
}

function navigateToDocs() {
    menuVisible.value = false
    solutionsMenuVisible.value = false
    void router.push('/docs')
}

function navigateToAbout() {
    menuVisible.value = false
    solutionsMenuVisible.value = false
    void router.push('/about')
}

function navigateToPricing() {
    menuVisible.value = false
    solutionsMenuVisible.value = false
    void router.push('/pricing')
}

function openContact() {
    menuVisible.value = false
    solutionsMenuVisible.value = false
    contactVisible.value = true
}

function closeSolutionsMenu(pointerEvent: PointerEvent) {
    const pointerTarget = pointerEvent.target
    if (!(pointerTarget instanceof Node)) return

    const clickedInsideMenu = Array.from(
        document.querySelectorAll('.solutions-menu-host'),
    ).some((menuHost) => menuHost.contains(pointerTarget))

    if (!clickedInsideMenu) solutionsMenuVisible.value = false
}

function closeSolutionsMenuWithKeyboard(keyboardEvent: KeyboardEvent) {
    if (keyboardEvent.key === 'Escape') solutionsMenuVisible.value = false
}

watch(
    () => router.currentRoute.value.fullPath,
    () => {
        menuVisible.value = false
        solutionsMenuVisible.value = false
    },
)

onMounted(() => {
    applyTheme(localStorage.getItem('pixels-theme') === 'dark' ? 'dark' : 'light')
    document.documentElement.lang = locale.value === 'zh' ? 'zh-CN' : 'en'
    document.addEventListener('pointerdown', closeSolutionsMenu)
    document.addEventListener('keydown', closeSolutionsMenuWithKeyboard)
})

onBeforeUnmount(() => {
    document.removeEventListener('pointerdown', closeSolutionsMenu)
    document.removeEventListener('keydown', closeSolutionsMenuWithKeyboard)
})
</script>

<template>
  <div class="site-frame">
    <header class="site-header">
      <div class="site-shell header-inner">
        <button class="brand" type="button" aria-label="PIXELS" @click="navigateTo()">
          <img :src="pixelsLogo" alt="">
          <span>PI<b class="brand-accent">X</b>ELS</span>
        </button>

        <nav class="desktop-nav" :aria-label="t('nav.home')">
          <button
            type="button"
            :class="{ active: activeNavigation === 'home' }"
            @click="navigateTo()"
          >
            {{ t('nav.home') }}
          </button>
          <div class="solutions-menu-host desktop-solutions-menu">
            <button
              class="solutions-trigger"
              type="button"
              :class="{ active: activeNavigation === 'solutions' }"
              :aria-expanded="solutionsMenuVisible"
              aria-haspopup="menu"
              @click.stop="solutionsMenuVisible = !solutionsMenuVisible"
            >
              {{ t('site.nav.solutions') }}
              <IconChevronDown
                :class="{ rotated: solutionsMenuVisible }"
                :size="15"
                :stroke-width="1.8"
              />
            </button>
            <Transition name="solutions-dropdown">
              <div v-if="solutionsMenuVisible" class="solutions-dropdown" role="menu">
                <button
                  v-for="(solutionItem, solutionIndex) in solutionMenuItems"
                  :key="solutionItem.key"
                  type="button"
                  role="menuitem"
                  @click="navigateToSolution(solutionItem.path)"
                >
                  <span class="dropdown-number">0{{ solutionIndex + 1 }}</span>
                  <span class="dropdown-icon">
                    <component :is="solutionItem.icon" :size="21" :stroke-width="1.6" />
                  </span>
                  <span class="dropdown-copy">
                    <strong>{{ t(`site.solutionPages.${solutionItem.key}.title`) }}</strong>
                    <small>{{ t(`site.solutionPages.${solutionItem.key}.menuDescription`) }}</small>
                  </span>
                  <IconArrowUpRight :size="16" :stroke-width="1.7" />
                </button>
              </div>
            </Transition>
          </div>
          <button
            type="button"
            :class="{ active: activeNavigation === 'downloads' }"
            @click="navigateToDownloads"
          >
            {{ t('site.nav.downloads') }}
          </button>
          <button
            type="button"
            :class="{ active: activeNavigation === 'docs' }"
            @click="navigateToDocs"
          >
            {{ t('site.nav.docs') }}
          </button>
          <button
            type="button"
            :class="{ active: activeNavigation === 'pricing' }"
            @click="navigateToPricing"
          >
            {{ t('site.nav.pricing') }}
          </button>
          <button
            type="button"
            :class="{ active: activeNavigation === 'about' }"
            @click="navigateToAbout"
          >
            {{ t('site.nav.about') }}
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
          <button
            type="button"
            :class="{ active: activeNavigation === 'home' }"
            @click="navigateTo()"
          >
            {{ t('nav.home') }}
          </button>
          <div class="solutions-menu-host mobile-solutions-menu">
            <button
              class="mobile-solutions-trigger"
              type="button"
              :class="{ active: activeNavigation === 'solutions' }"
              :aria-expanded="solutionsMenuVisible"
              @click.stop="solutionsMenuVisible = !solutionsMenuVisible"
            >
              {{ t('site.nav.solutions') }}
              <IconChevronDown
                :class="{ rotated: solutionsMenuVisible }"
                :size="16"
                :stroke-width="1.8"
              />
            </button>
            <div v-if="solutionsMenuVisible" class="mobile-solution-links">
              <button
                v-for="solutionItem in solutionMenuItems"
                :key="solutionItem.key"
                type="button"
                @click="navigateToSolution(solutionItem.path)"
              >
                <component :is="solutionItem.icon" :size="18" :stroke-width="1.6" />
                {{ t(`site.solutionPages.${solutionItem.key}.title`) }}
                <IconArrowUpRight :size="14" :stroke-width="1.7" />
              </button>
            </div>
          </div>
          <button
            type="button"
            :class="{ active: activeNavigation === 'downloads' }"
            @click="navigateToDownloads"
          >
            {{ t('site.nav.downloads') }}
          </button>
          <button
            type="button"
            :class="{ active: activeNavigation === 'docs' }"
            @click="navigateToDocs"
          >
            {{ t('site.nav.docs') }}
          </button>
          <button
            type="button"
            :class="{ active: activeNavigation === 'pricing' }"
            @click="navigateToPricing"
          >
            {{ t('site.nav.pricing') }}
          </button>
          <button
            type="button"
            :class="{ active: activeNavigation === 'about' }"
            @click="navigateToAbout"
          >
            {{ t('site.nav.about') }}
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
            <span>PI<b class="brand-accent">X</b>ELS</span>
          </div>
          <p>{{ t('site.footer.description') }}</p>
          <strong>{{ t('site.footer.tagline') }}</strong>
        </div>
        <div class="footer-column">
          <span>{{ t('site.nav.solutions') }}</span>
          <button type="button" @click="navigateToSolution('/solutions/remote-desktop')">
            {{ t('site.solutionPages.remote.title') }}
          </button>
          <button type="button" @click="navigateToSolution('/solutions/cloud-gaming')">
            {{ t('site.solutionPages.gaming.title') }}
          </button>
          <button type="button" @click="navigateToSolution('/solutions/cloud-rendering')">
            {{ t('site.solutionPages.rendering.title') }}
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
          <button type="button" @click="navigateToPricing">
            {{ t('site.nav.pricing') }}
          </button>
          <button type="button" @click="navigateToAbout">
            {{ t('site.nav.about') }}
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
    color: color-mix(in srgb, var(--foreground) 82%, var(--background));
    font-family: "10 Pixel", sans-serif;
    font-size: 24px;
    font-weight: 700;
    letter-spacing: 0.08em;
    line-height: 1;
    transform: translateY(3px);
    -webkit-text-stroke: 0.4px currentColor;
}

.brand-accent {
    color: #087e74;
    font-weight: inherit;
    -webkit-text-stroke-color: #087e74;
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

.desktop-solutions-menu {
    position: relative;
    display: flex;
    align-self: stretch;
    align-items: center;
}

.solutions-trigger {
    gap: 5px;
}

.solutions-trigger svg,
.mobile-solutions-trigger svg {
    transition: transform 180ms ease;
}

.solutions-trigger svg.rotated,
.mobile-solutions-trigger svg.rotated {
    transform: rotate(180deg);
}

.solutions-dropdown {
    position: absolute;
    z-index: 60;
    top: calc(100% - 7px);
    left: -18px;
    display: grid;
    width: 390px;
    padding: 9px;
    border: 1px solid var(--border);
    border-radius: 16px;
    background: color-mix(in srgb, var(--card) 96%, transparent);
    box-shadow: 0 24px 64px rgba(15, 23, 42, 0.16);
    backdrop-filter: blur(20px) saturate(150%);
}

.desktop-nav .solutions-dropdown button {
    display: grid;
    min-height: 76px;
    height: auto;
    grid-template-columns: 24px 42px 1fr 18px;
    align-items: center;
    gap: 12px;
    padding: 11px 13px;
    border-radius: 11px;
    color: var(--foreground);
    text-align: left;
}

.desktop-nav .solutions-dropdown button:hover {
    background: var(--accent);
}

.dropdown-number {
    align-self: start;
    padding-top: 4px;
    color: var(--muted-foreground);
    font: 10px var(--font-tech);
}

.dropdown-icon {
    display: grid;
    width: 42px;
    height: 42px;
    place-items: center;
    border-radius: 11px;
    background: var(--accent);
    color: var(--primary);
}

.dropdown-copy {
    display: grid;
    min-width: 0;
    gap: 5px;
}

.dropdown-copy strong {
    color: var(--foreground);
    font-size: 14px;
    font-weight: 720;
}

.dropdown-copy small {
    overflow: hidden;
    color: var(--muted-foreground);
    font-size: 11px;
    line-height: 1.45;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.solutions-dropdown > button > svg {
    color: var(--muted-foreground);
}

.solutions-dropdown-enter-active,
.solutions-dropdown-leave-active {
    transition:
        opacity 160ms ease,
        transform 160ms ease;
    transform-origin: top left;
}

.solutions-dropdown-enter-from,
.solutions-dropdown-leave-to {
    opacity: 0;
    transform: translateY(-6px) scale(0.985);
}

.desktop-nav button:hover,
.footer-column button:hover {
    color: var(--primary);
}

.desktop-nav > button,
.solutions-trigger {
    position: relative;
}

.desktop-nav > button.active,
.solutions-trigger.active {
    color: var(--primary);
    font-weight: 750;
}

.desktop-nav > button.active::after,
.solutions-trigger.active::after {
    position: absolute;
    bottom: -17px;
    left: 50%;
    width: 30px;
    height: 2px;
    border-radius: 2px 2px 0 0;
    background: var(--primary);
    content: '';
    transform: translateX(-50%);
}

.solutions-trigger.active::after {
    left: calc(50% - 10px);
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

    .mobile-nav > button.active,
    .mobile-solutions-trigger.active {
        color: var(--primary);
        font-weight: 750;
    }

    .mobile-solutions-trigger {
        display: flex;
        width: 100%;
        align-items: center;
        justify-content: space-between;
    }

    .mobile-solution-links {
        display: grid;
        gap: 6px;
        padding: 7px 0 10px 14px;
        border-bottom: 1px solid var(--border);
    }

    .mobile-nav .mobile-solution-links button {
        display: grid;
        grid-template-columns: 28px 1fr 16px;
        align-items: center;
        gap: 9px;
        padding: 11px 12px;
        border: 0;
        border-radius: 9px;
        background: var(--secondary);
        color: var(--secondary-foreground);
        font-size: 13px;
    }

    .mobile-solution-links button > svg:first-child {
        color: var(--primary);
    }

    .mobile-solution-links button > svg:last-child {
        color: var(--muted-foreground);
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
