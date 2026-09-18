<script setup lang="ts">
import { computed, ref, type Component } from 'vue'
import { useI18n } from 'vue-i18n'
import {
    IconArrowDown,
    IconArrowUpRight,
    IconBrandAndroid,
    IconBrandApple,
    IconBrandWindows,
    IconCheck,
    IconCloudComputing,
    IconDeviceDesktop,
    IconDeviceMobile,
    IconFileCertificate,
    IconFileDescription,
    IconHash,
    IconServer2,
    IconTerminal2,
} from '@tabler/icons-vue'
import ContactUs from '@/components/ContactUs.vue'
import pixelsLogo from '@/assets/pixels-logo-45.svg'

type PlatformKey = 'windows' | 'linux' | 'macos' | 'android' | 'ios'
type ProductKey = 'server' | 'cloudNode' | 'remote' | 'liteClient'
type CategoryKey = 'infrastructure' | 'compute' | 'fullClient' | 'liteClient'

interface PlatformDefinition {
    key: PlatformKey
    icon: Component
}

interface ProductDefinition {
    key: ProductKey
    category: CategoryKey
    number: string
    icon: Component
    platforms: PlatformKey[]
    tone: string
}

interface DownloadMetric {
    value: string
    label: string
}

const { t, tm } = useI18n()
const contactVisible = ref(false)

const platformDefinitions: Record<PlatformKey, PlatformDefinition> = {
    windows: { key: 'windows', icon: IconBrandWindows },
    linux: { key: 'linux', icon: IconTerminal2 },
    macos: { key: 'macos', icon: IconBrandApple },
    android: { key: 'android', icon: IconBrandAndroid },
    ios: { key: 'ios', icon: IconBrandApple },
}

const productDefinitions: ProductDefinition[] = [
    {
        key: 'server',
        category: 'infrastructure',
        number: '01',
        icon: IconServer2,
        platforms: ['windows', 'linux'],
        tone: 'green',
    },
    {
        key: 'cloudNode',
        category: 'compute',
        number: '02',
        icon: IconCloudComputing,
        platforms: ['windows', 'linux'],
        tone: 'cyan',
    },
    {
        key: 'remote',
        category: 'fullClient',
        number: '03',
        icon: IconDeviceDesktop,
        platforms: ['windows', 'linux', 'macos', 'android', 'ios'],
        tone: 'blue',
    },
    {
        key: 'liteClient',
        category: 'liteClient',
        number: '04',
        icon: IconDeviceMobile,
        platforms: ['windows', 'linux', 'macos', 'android', 'ios'],
        tone: 'violet',
    },
]

const downloadMetrics = computed(() => tm('site.downloads.metrics') as DownloadMetric[])

function platformDefinition(platformKey: PlatformKey) {
    return platformDefinitions[platformKey]
}

function scrollToPackages() {
    document.querySelector('#download-packages')?.scrollIntoView({ behavior: 'smooth' })
}
</script>

<template>
  <ContactUs v-model="contactVisible" />
  <div class="downloads-page">
    <section class="hero-section">
      <div class="hero-grid" aria-hidden="true" />
      <div class="hero-glow" aria-hidden="true" />
      <div class="page-shell hero-layout">
        <div class="hero-copy">
          <div class="hero-kicker">
            <span class="live-dot" />
            {{ t('site.downloads.eyebrow') }}
          </div>
          <h1>
            <span>{{ t('site.downloads.title') }}</span>
            <strong>{{ t('site.downloads.titleAccent') }}</strong>
          </h1>
          <p>{{ t('site.downloads.description') }}</p>
          <div class="hero-actions">
            <button class="button-primary" type="button" @click="scrollToPackages">
              {{ t('site.downloads.packagesEyebrow') }}
              <IconArrowDown :size="17" :stroke-width="1.9" />
            </button>
            <button class="button-secondary" type="button" @click="contactVisible = true">
              {{ t('site.downloads.help.action') }}
              <IconArrowUpRight :size="17" :stroke-width="1.9" />
            </button>
          </div>
          <div class="hero-trust">
            <IconCheck :size="14" :stroke-width="2" />
            {{ t('site.downloads.releaseNotice') }}
          </div>
        </div>

        <div class="download-constellation" aria-hidden="true">
          <div class="constellation-aura" />
          <div class="constellation-ring ring-one" />
          <div class="constellation-ring ring-two" />
          <div class="constellation-core">
            <img :src="pixelsLogo" alt="">
            <span>PIXELS</span>
          </div>
          <div
            v-for="(product, productIndex) in productDefinitions"
            :key="product.key"
            class="constellation-product"
            :class="[`product-position-${productIndex + 1}`, `tone-${product.tone}`]"
          >
            <component :is="product.icon" :size="24" :stroke-width="1.55" />
            <span>0{{ productIndex + 1 }}</span>
          </div>
          <div class="platform-ribbon">
            <span
              v-for="platform in Object.values(platformDefinitions)"
              :key="platform.key"
            >
              <component :is="platform.icon" :size="17" :stroke-width="1.7" />
              {{ t(`site.downloads.platforms.${platform.key}`) }}
            </span>
          </div>
        </div>
      </div>
    </section>

    <section class="metrics-section">
      <div class="page-shell metrics-grid">
        <div v-for="downloadMetric in downloadMetrics" :key="downloadMetric.value" class="metric">
          <strong>{{ downloadMetric.value }}</strong>
          <span>{{ downloadMetric.label }}</span>
        </div>
      </div>
    </section>

    <section id="download-packages" class="section packages-section">
      <div class="page-shell">
        <div class="section-heading">
          <span>{{ t('site.downloads.packagesEyebrow') }}</span>
          <h2>{{ t('site.downloads.packagesTitle') }}</h2>
          <p>{{ t('site.downloads.packagesDescription') }}</p>
        </div>

        <div class="package-stack">
          <article
            v-for="(product, productIndex) in productDefinitions"
            :key="product.key"
            class="package-panel"
            :class="[`tone-${product.tone}`, { reversed: productIndex % 2 === 1 }]"
          >
            <div class="package-copy">
              <span class="package-number">PACKAGE {{ product.number }}</span>
              <div class="package-title">
                <span class="package-icon">
                  <component :is="product.icon" :size="29" :stroke-width="1.55" />
                </span>
                <div>
                  <small>{{ t(`site.downloads.category.${product.category}`) }}</small>
                  <h3>{{ t(`site.downloads.products.${product.key}.title`) }}</h3>
                </div>
              </div>
              <p>{{ t(`site.downloads.products.${product.key}.description`) }}</p>
              <span class="platform-label">{{ t('site.downloads.supportedSystems') }}</span>
              <div class="platform-list">
                <button
                  v-for="platformKey in product.platforms"
                  :key="platformKey"
                  type="button"
                  disabled
                >
                  <component
                    :is="platformDefinition(platformKey).icon"
                    :size="19"
                    :stroke-width="1.75"
                  />
                  {{ t(`site.downloads.platforms.${platformKey}`) }}
                </button>
              </div>
              <div class="package-status">
                <span />
                {{ t('site.downloads.comingSoon') }}
              </div>
            </div>

            <div class="package-visual">
              <div class="visual-grid" />
              <div class="visual-glow" />
              <div class="visual-window">
                <div class="window-bar">
                  <span><i /><i /><i /></span>
                  <small>PIXELS / {{ product.number }}</small>
                  <b>{{ t('site.downloads.comingSoon') }}</b>
                </div>
                <div class="window-content">
                  <div class="visual-product-icon">
                    <component :is="product.icon" :size="54" :stroke-width="1.25" />
                  </div>
                  <strong>{{ t(`site.downloads.products.${product.key}.title`) }}</strong>
                  <div class="visual-route">
                    <i /><i /><i /><i /><i />
                  </div>
                  <div class="visual-platforms">
                    <span
                      v-for="platformKey in product.platforms"
                      :key="platformKey"
                    >
                      <component
                        :is="platformDefinition(platformKey).icon"
                        :size="19"
                        :stroke-width="1.7"
                      />
                    </span>
                  </div>
                </div>
              </div>
            </div>
          </article>
        </div>
      </div>
    </section>

    <section class="security-section">
      <div class="page-shell security-layout">
        <div class="section-heading security-heading">
          <span>{{ t('site.downloads.security.eyebrow') }}</span>
          <h2>{{ t('site.downloads.security.title') }}</h2>
          <p>{{ t('site.downloads.security.description') }}</p>
        </div>
        <div class="security-grid">
          <article>
            <span>01</span>
            <div><IconFileCertificate :size="25" :stroke-width="1.6" /></div>
            <h3>{{ t('site.downloads.security.signed') }}</h3>
          </article>
          <article>
            <span>02</span>
            <div><IconHash :size="25" :stroke-width="1.6" /></div>
            <h3>{{ t('site.downloads.security.checksum') }}</h3>
          </article>
          <article>
            <span>03</span>
            <div><IconFileDescription :size="25" :stroke-width="1.6" /></div>
            <h3>{{ t('site.downloads.security.notes') }}</h3>
          </article>
        </div>
      </div>
    </section>

    <section class="page-shell final-cta">
      <div class="cta-grid" aria-hidden="true" />
      <div class="cta-orb" aria-hidden="true" />
      <div class="cta-copy">
        <span>{{ t('site.downloads.help.eyebrow') }}</span>
        <h2>{{ t('site.downloads.help.title') }}</h2>
        <p>{{ t('site.downloads.help.description') }}</p>
      </div>
      <button type="button" @click="contactVisible = true">
        {{ t('site.downloads.help.action') }}
        <IconArrowUpRight :size="18" :stroke-width="1.9" />
      </button>
    </section>
  </div>
</template>

<style scoped>
.downloads-page {
    --background: #fafafa;
    --foreground: #18181b;
    --card: #ffffff;
    --primary: #007f49;
    --primary-strong: #006b3d;
    --primary-bright: #009a59;
    --primary-foreground: #ffffff;
    --secondary: #f4f4f5;
    --secondary-foreground: #27272a;
    --muted-foreground: #71717a;
    --accent: #ecfdf5;
    --border: #e4e4e7;
    overflow: hidden;
    background: var(--background);
    color: var(--foreground);
}

.page-shell { width: min(1200px, calc(100% - 48px)); margin: 0 auto; }

.hero-section {
    position: relative;
    min-height: 700px;
    overflow: hidden;
    border-bottom: 1px solid var(--border);
    background:
        radial-gradient(circle at 78% 32%, rgba(0, 154, 89, 0.12), transparent 31%),
        linear-gradient(180deg, var(--background), color-mix(in srgb, var(--accent) 46%, var(--background)));
}

.hero-grid,
.visual-grid,
.cta-grid {
    position: absolute;
    inset: 0;
    background-image:
        linear-gradient(to right, rgba(0, 127, 73, 0.055) 1px, transparent 1px),
        linear-gradient(to bottom, rgba(0, 127, 73, 0.055) 1px, transparent 1px);
    background-size: 42px 42px;
    mask-image: linear-gradient(90deg, transparent, black 50%, transparent);
}

.hero-glow {
    position: absolute;
    top: 120px;
    right: 5%;
    width: 480px;
    height: 480px;
    border-radius: 50%;
    background: rgba(0, 154, 89, 0.08);
    filter: blur(80px);
}

.hero-layout {
    position: relative;
    z-index: 2;
    display: grid;
    min-height: 700px;
    grid-template-columns: minmax(0, 1fr) minmax(480px, 0.94fr);
    align-items: center;
    gap: 76px;
    padding: 70px 0 78px;
}

.hero-kicker,
.section-heading > span,
.cta-copy > span {
    display: flex;
    align-items: center;
    gap: 9px;
    color: var(--primary);
    font: 700 11px var(--font-tech);
    letter-spacing: 0.13em;
    text-transform: uppercase;
}

.live-dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--primary-bright);
    box-shadow: 0 0 0 5px rgba(0, 154, 89, 0.12);
}

.hero-copy h1 {
    margin: 24px 0 22px;
    font: 750 clamp(52px, 5.2vw, 78px) / 1.04 var(--font-ui);
    letter-spacing: -0.065em;
}

.hero-copy h1 span,
.hero-copy h1 strong { display: block; }
.hero-copy h1 strong { color: var(--primary); font-weight: 750; }

.hero-copy > p {
    max-width: 590px;
    margin: 0;
    color: var(--muted-foreground);
    font-size: 16px;
    line-height: 1.85;
}

.hero-actions { display: flex; gap: 12px; margin-top: 34px; }

.button-primary,
.button-secondary,
.final-cta > button {
    display: inline-flex;
    min-height: 48px;
    align-items: center;
    justify-content: center;
    gap: 11px;
    padding: 0 19px;
    border-radius: 11px;
    cursor: pointer;
    font: 700 14px var(--font-ui);
    transition: transform 160ms ease, box-shadow 160ms ease;
}

.button-primary {
    border: 1px solid var(--primary);
    background: var(--primary);
    box-shadow: 0 14px 32px rgba(0, 127, 73, 0.2);
    color: var(--primary-foreground);
}

.button-secondary { border: 1px solid var(--border); background: var(--card); color: var(--foreground); }
.button-primary:hover,
.button-secondary:hover,
.final-cta > button:hover { transform: translateY(-2px); }

.hero-trust {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-top: 34px;
    color: var(--muted-foreground);
    font: 700 11px var(--font-tech);
    letter-spacing: 0.08em;
}

.hero-trust svg { color: var(--primary); }

.download-constellation { position: relative; height: 520px; }
.constellation-aura {
    position: absolute;
    inset: 15%;
    border-radius: 50%;
    background: radial-gradient(circle, rgba(0, 154, 89, 0.18), transparent 67%);
    filter: blur(8px);
}

.constellation-ring {
    position: absolute;
    top: 50%;
    left: 50%;
    border: 1px dashed rgba(0, 127, 73, 0.25);
    border-radius: 50%;
    transform: translate(-50%, -50%);
}

.ring-one { width: 330px; height: 330px; animation: orbit 28s linear infinite; }
.ring-two { width: 210px; height: 210px; animation: orbit 20s linear infinite reverse; }

.constellation-core {
    position: absolute;
    top: 50%;
    left: 50%;
    display: grid;
    width: 132px;
    height: 132px;
    place-items: center;
    border: 1px solid rgba(0, 127, 73, 0.18);
    border-radius: 34px;
    background: var(--card);
    box-shadow: 0 28px 60px rgba(0, 91, 53, 0.16);
    transform: translate(-50%, -50%) rotate(45deg);
}

.constellation-core img,
.constellation-core span { transform: rotate(-45deg); }
.constellation-core img { width: 53px; height: 53px; }
.constellation-core span { margin-top: -26px; color: var(--primary); font: 700 10px "10 Pixel", var(--font-tech); }

.constellation-product {
    position: absolute;
    display: grid;
    width: 68px;
    height: 68px;
    place-items: center;
    border: 1px solid var(--border);
    border-radius: 18px;
    background: var(--card);
    box-shadow: 0 15px 34px rgba(24, 24, 27, 0.1);
    color: var(--tone);
}

.constellation-product span { position: absolute; right: 7px; bottom: 5px; color: var(--muted-foreground); font: 7px var(--font-tech); }
.product-position-1 { top: 54px; left: 50%; transform: translateX(-50%); }
.product-position-2 { top: 50%; right: 24px; transform: translateY(-50%); }
.product-position-3 { bottom: 70px; left: 50%; transform: translateX(-50%); }
.product-position-4 { top: 50%; left: 24px; transform: translateY(-50%); }

.platform-ribbon {
    position: absolute;
    right: 5%;
    bottom: 8px;
    left: 5%;
    display: flex;
    min-height: 56px;
    align-items: center;
    justify-content: center;
    gap: 18px;
    border: 1px solid rgba(0, 127, 73, 0.14);
    border-radius: 16px;
    background: color-mix(in srgb, var(--card) 82%, transparent);
    box-shadow: 0 18px 38px rgba(0, 63, 36, 0.08);
    backdrop-filter: blur(14px);
}

.platform-ribbon span { display: inline-flex; align-items: center; gap: 5px; color: var(--muted-foreground); font-size: 10px; }

.tone-green { --tone: #008f52; --tone-soft: #e8f8ef; --tone-glow: rgba(0, 154, 89, 0.28); }
.tone-cyan { --tone: #0d9f92; --tone-soft: #e7f8f6; --tone-glow: rgba(13, 159, 146, 0.28); }
.tone-blue { --tone: #3976d8; --tone-soft: #edf4ff; --tone-glow: rgba(57, 118, 216, 0.3); }
.tone-violet { --tone: #7959c7; --tone-soft: #f2eefc; --tone-glow: rgba(121, 89, 199, 0.3); }

.metrics-section { border-bottom: 1px solid var(--border); background: var(--card); }
.metrics-grid { display: grid; grid-template-columns: repeat(3, 1fr); }
.metric { display: grid; min-height: 112px; grid-template-columns: auto 1fr; align-items: center; gap: 17px; padding: 24px 44px; border-right: 1px solid var(--border); }
.metric:first-child { padding-left: 0; }
.metric:last-child { border-right: 0; }
.metric strong { color: var(--primary); font-size: 22px; white-space: nowrap; }
.metric span { color: var(--muted-foreground); font-size: 12px; line-height: 1.55; }

.section { padding: 120px 0; }
.section-heading h2 { max-width: 720px; margin: 17px 0 18px; font: 730 clamp(34px, 4vw, 52px) / 1.16 var(--font-ui); letter-spacing: -0.052em; }
.section-heading p { max-width: 650px; margin: 0; color: var(--muted-foreground); font-size: 15px; line-height: 1.8; }
.package-stack { display: grid; gap: 22px; margin-top: 58px; }

.package-panel {
    display: grid;
    min-height: 430px;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 22px;
    background: var(--card);
    box-shadow: 0 14px 45px rgba(24, 24, 27, 0.045);
}

.package-panel.reversed .package-copy { order: 2; }
.package-copy { display: flex; align-items: flex-start; justify-content: center; flex-direction: column; padding: 48px 56px; }
.package-number { color: var(--tone); font: 700 14px "10 Pixel", sans-serif; letter-spacing: 0.08em; }
.package-title { display: flex; align-items: center; gap: 15px; margin: 22px 0 15px; }
.package-icon { display: grid; width: 52px; height: 52px; place-items: center; border-radius: 14px; background: var(--tone-soft); color: var(--tone); }
.package-title small { color: var(--tone); font: 700 10px var(--font-tech); letter-spacing: 0.08em; text-transform: uppercase; }
.package-title h3 { margin: 4px 0 0; font-size: 31px; letter-spacing: -0.035em; }
.package-copy > p { max-width: 460px; margin: 0; color: var(--muted-foreground); font-size: 14px; line-height: 1.78; }
.platform-label { margin-top: 27px; color: var(--muted-foreground); font: 700 10px var(--font-tech); letter-spacing: 0.08em; text-transform: uppercase; }
.platform-list { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 11px; }
.platform-list button { display: inline-flex; min-height: 38px; align-items: center; gap: 7px; padding: 0 11px; border: 1px solid var(--border); border-radius: 10px; background: var(--background); color: var(--secondary-foreground); opacity: 1; font: 650 11px var(--font-ui); }
.platform-list button svg { color: var(--tone); }
.package-status { display: flex; align-items: center; gap: 8px; margin-top: 19px; color: var(--muted-foreground); font: 650 10px var(--font-tech); letter-spacing: 0.07em; }
.package-status span { width: 6px; height: 6px; border-radius: 50%; background: var(--tone); box-shadow: 0 0 0 4px color-mix(in srgb, var(--tone) 13%, transparent); }

.package-visual { position: relative; min-height: 430px; overflow: hidden; isolation: isolate; background: linear-gradient(145deg, var(--tone-soft), #eefbf5 52%, #eef3ff); }
.visual-grid { z-index: -1; background-size: 30px 30px; mask-image: linear-gradient(135deg, transparent 2%, black 24%, black 72%, transparent 96%); }
.visual-glow { position: absolute; right: 7%; bottom: -18%; left: 7%; height: 48%; border-radius: 50%; background: linear-gradient(90deg, var(--tone-glow), rgba(14, 165, 233, 0.22), rgba(168, 85, 247, 0.22)); filter: blur(42px); }
.visual-window { position: absolute; top: 15%; right: 8%; bottom: 15%; left: 8%; overflow: hidden; border: 1px solid rgba(255, 255, 255, 0.75); border-radius: 18px; background: color-mix(in srgb, var(--card) 90%, transparent); box-shadow: 0 28px 55px rgba(15, 23, 42, 0.16); backdrop-filter: blur(12px); transition: transform 320ms ease; }
.package-panel:hover .visual-window { transform: translateY(-5px); }
.window-bar { display: grid; height: 38px; grid-template-columns: 1fr auto 1fr; align-items: center; padding: 0 13px; border-bottom: 1px solid var(--border); color: var(--muted-foreground); }
.window-bar > span { display: flex; gap: 5px; }
.window-bar i { width: 6px; height: 6px; border-radius: 50%; background: var(--border); }
.window-bar i:first-child { background: var(--tone); }
.window-bar small,
.window-bar b { font: 700 8px var(--font-tech); letter-spacing: 0.09em; }
.window-bar b { justify-self: end; color: var(--tone); }
.window-content { position: relative; display: flex; height: calc(100% - 38px); align-items: center; justify-content: center; flex-direction: column; }
.visual-product-icon { display: grid; width: 110px; height: 110px; place-items: center; border-radius: 28px; background: var(--tone-soft); box-shadow: 0 20px 40px color-mix(in srgb, var(--tone) 18%, transparent); color: var(--tone); }
.window-content > strong { margin-top: 18px; font-size: 15px; }
.visual-route { display: flex; align-items: center; gap: 19px; margin: 24px 0 16px; }
.visual-route::before,
.visual-route::after { width: 70px; height: 1px; background: linear-gradient(90deg, transparent, var(--tone)); content: ''; }
.visual-route::after { transform: rotate(180deg); }
.visual-route i { width: 5px; height: 5px; border-radius: 50%; background: var(--tone); animation: pulse 2s ease-in-out infinite; }
.visual-route i:nth-child(2) { animation-delay: 120ms; }
.visual-route i:nth-child(3) { animation-delay: 240ms; }
.visual-route i:nth-child(4) { animation-delay: 360ms; }
.visual-route i:nth-child(5) { animation-delay: 480ms; }
.visual-platforms { display: flex; gap: 9px; }
.visual-platforms span { display: grid; width: 37px; height: 37px; place-items: center; border: 1px solid var(--border); border-radius: 10px; background: var(--card); color: var(--tone); }

.security-section { padding: 120px 0; border-block: 1px solid var(--border); background: var(--secondary); }
.security-layout { display: grid; grid-template-columns: 0.75fr 1.25fr; gap: 75px; }
.security-heading { position: sticky; top: 130px; }
.security-grid { display: grid; grid-template-columns: repeat(3, 1fr); overflow: hidden; border: 1px solid var(--border); border-radius: 20px; background: var(--border); gap: 1px; }
.security-grid article { position: relative; min-height: 260px; padding: 31px; background: var(--card); }
.security-grid article > span { position: absolute; top: 31px; right: 31px; color: var(--muted-foreground); font: 10px var(--font-tech); }
.security-grid article > div { display: grid; width: 44px; height: 44px; place-items: center; border-radius: 12px; background: var(--accent); color: var(--primary); }
.security-grid h3 { margin: 105px 0 0; font-size: 17px; }

.final-cta { position: relative; display: flex; min-height: 330px; align-items: center; justify-content: space-between; gap: 55px; margin-block: 120px; padding: 62px 68px; overflow: hidden; border-radius: 24px; background: linear-gradient(125deg, #052e22, #007f49 70%, #009a59); color: #fff; }
.cta-grid { opacity: .35; mask-image: linear-gradient(90deg, transparent, black); }
.cta-orb { position: absolute; right: -100px; width: 330px; height: 330px; border: 1px solid rgba(255,255,255,.2); border-radius: 50%; box-shadow: 0 0 0 55px rgba(255,255,255,.04), 0 0 0 110px rgba(255,255,255,.025); }
.cta-copy { position: relative; z-index: 1; max-width: 700px; }
.cta-copy > span { color: #8ceec0; }
.cta-copy h2 { margin: 17px 0 15px; font: 730 clamp(33px, 4vw, 50px) / 1.14 var(--font-ui); letter-spacing: -0.05em; }
.cta-copy p { max-width: 600px; margin: 0; color: rgba(255,255,255,.72); font-size: 14px; line-height: 1.75; }
.final-cta > button { position: relative; z-index: 1; flex: 0 0 auto; border: 0; background: #fff; color: #006b3d; }

@keyframes orbit { to { transform: translate(-50%, -50%) rotate(360deg); } }
@keyframes pulse { 50% { opacity: .3; transform: scale(.7); } }

:global(html[data-theme='dark'] .downloads-page) {
    --background: #09090b;
    --foreground: #fafafa;
    --card: #101014;
    --primary: #009a59;
    --primary-strong: #8ceec0;
    --primary-bright: #20c77a;
    --primary-foreground: #fff;
    --secondary: #141416;
    --secondary-foreground: #f4f4f5;
    --muted-foreground: #a1a1aa;
    --accent: #052e22;
    --border: #27272a;
}

:global(html[data-theme='dark'] .tone-green) { --tone-soft: #0a3021; }
:global(html[data-theme='dark'] .tone-cyan) { --tone-soft: #092d2b; }
:global(html[data-theme='dark'] .tone-blue) { --tone-soft: #10233e; }
:global(html[data-theme='dark'] .tone-violet) { --tone-soft: #271d40; }

@media (prefers-reduced-motion: reduce) {
    .constellation-ring,
    .visual-route i { animation: none; }
}

@media (max-width: 1020px) {
    .hero-layout { grid-template-columns: 1fr; gap: 20px; padding-top: 100px; }
    .download-constellation { width: min(620px, 100%); justify-self: center; }
    .package-copy { padding: 44px; }
    .security-layout { grid-template-columns: 1fr; }
    .security-heading { position: static; }
}

@media (max-width: 820px) {
    .page-shell { width: min(100% - 32px, 680px); }
    .metrics-grid { grid-template-columns: 1fr; }
    .metric { min-height: 88px; padding: 20px 0; border-right: 0; border-bottom: 1px solid var(--border); }
    .metric:last-child { border-bottom: 0; }
    .package-panel { grid-template-columns: 1fr; }
    .package-panel.reversed .package-copy { order: 0; }
    .package-visual { min-height: 390px; }
    .security-grid { grid-template-columns: 1fr; }
    .security-grid article { min-height: 170px; }
    .security-grid h3 { margin-top: 60px; }
    .final-cta { align-items: flex-start; flex-direction: column; margin-block: 90px; padding: 48px 38px; }
}

@media (max-width: 540px) {
    .hero-section,
    .hero-layout { min-height: auto; }
    .hero-copy h1 { font-size: 48px; }
    .hero-actions { align-items: stretch; flex-direction: column; }
    .download-constellation { height: 430px; }
    .ring-one { width: 270px; height: 270px; }
    .ring-two { width: 170px; height: 170px; }
    .constellation-core { width: 110px; height: 110px; }
    .constellation-product { width: 58px; height: 58px; }
    .product-position-1 { top: 42px; }
    .product-position-2 { right: 8px; }
    .product-position-3 { bottom: 68px; }
    .product-position-4 { left: 8px; }
    .platform-ribbon { right: 0; left: 0; flex-wrap: wrap; gap: 8px 13px; padding: 10px; }
    .section { padding: 90px 0; }
    .package-copy { padding: 36px 26px; }
    .package-visual { min-height: 330px; }
    .visual-window { top: 10%; right: 5%; bottom: 10%; left: 5%; }
    .visual-product-icon { width: 85px; height: 85px; }
    .visual-route::before,
    .visual-route::after { width: 25px; }
    .final-cta { padding: 40px 26px; }
}
</style>
