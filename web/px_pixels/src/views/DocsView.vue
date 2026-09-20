<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, type Component } from 'vue'
import { useI18n } from 'vue-i18n'
import {
    IconArrowRight,
    IconArrowUpRight,
    IconBook2,
    IconChevronRight,
    IconCube3dSphere,
    IconDeviceDesktop,
    IconDeviceGamepad2,
    IconRoute,
    IconSearch,
} from '@tabler/icons-vue'
import ContactUs from '@/components/ContactUs.vue'
import pixelsLogo from '@/assets/pixels-logo-45.svg'

type ProductKey = 'remote' | 'gaming' | 'rendering'

interface ProductDefinition {
    key: ProductKey
    number: string
    icon: Component
    tone: string
}

interface DocumentationSection {
    title: string
    description: string
    articles: string[]
}

interface ProductDocumentation {
    title: string
    subtitle: string
    description: string
    quickSteps: string[]
    sections: DocumentationSection[]
}

interface VisibleProduct {
    definition: ProductDefinition
    content: ProductDocumentation
    sections: DocumentationSection[]
}

const { t, tm } = useI18n()
const searchText = ref('')
const contactVisible = ref(false)
const heroSearchInput = ref<HTMLInputElement>()

const productDefinitions: ProductDefinition[] = [
    { key: 'remote', number: '01', icon: IconDeviceDesktop, tone: 'green' },
    { key: 'gaming', number: '02', icon: IconDeviceGamepad2, tone: 'blue' },
    { key: 'rendering', number: '03', icon: IconCube3dSphere, tone: 'violet' },
]

const visibleProducts = computed<VisibleProduct[]>(() => {
    const normalizedSearch = searchText.value.trim().toLocaleLowerCase()

    return productDefinitions.flatMap((productDefinition) => {
        const productContent = tm(
            `site.docs.products.${productDefinition.key}`,
        ) as ProductDocumentation

        if (!normalizedSearch) {
            return [{
                definition: productDefinition,
                content: productContent,
                sections: productContent.sections,
            }]
        }

        const productMatches = [
            productContent.title,
            productContent.subtitle,
            productContent.description,
        ].some((productText) => productText.toLocaleLowerCase().includes(normalizedSearch))

        const matchingSections = productContent.sections
            .map((documentationSection) => ({
                ...documentationSection,
                articles: productMatches
                    ? documentationSection.articles
                    : documentationSection.articles.filter((articleTitle) => (
                        articleTitle.toLocaleLowerCase().includes(normalizedSearch)
                        || documentationSection.title.toLocaleLowerCase().includes(normalizedSearch)
                        || documentationSection.description.toLocaleLowerCase().includes(normalizedSearch)
                    )),
            }))
            .filter((documentationSection) => documentationSection.articles.length > 0)

        if (!productMatches && matchingSections.length === 0) return []

        return [{
            definition: productDefinition,
            content: productContent,
            sections: matchingSections,
        }]
    })
})

function productContent(productKey: ProductKey) {
    return tm(`site.docs.products.${productKey}`) as ProductDocumentation
}

function scrollToProduct(productKey: ProductKey) {
    document.querySelector(`#docs-${productKey}`)?.scrollIntoView({ behavior: 'smooth' })
}

function focusDocumentationSearch(keyboardEvent: KeyboardEvent) {
    if (keyboardEvent.key !== '/' || keyboardEvent.metaKey || keyboardEvent.ctrlKey) return
    if (keyboardEvent.target instanceof HTMLInputElement) return
    if (keyboardEvent.target instanceof HTMLTextAreaElement) return

    keyboardEvent.preventDefault()
    heroSearchInput.value?.focus()
}

onMounted(() => window.addEventListener('keydown', focusDocumentationSearch))
onBeforeUnmount(() => window.removeEventListener('keydown', focusDocumentationSearch))
</script>

<template>
  <ContactUs v-model="contactVisible" />
  <div class="docs-page">
    <section class="hero-section">
      <div class="hero-grid" aria-hidden="true" />
      <div class="hero-glow" aria-hidden="true" />
      <div class="page-shell hero-layout">
        <div class="hero-copy">
          <div class="hero-kicker">
            <span class="live-dot" />
            {{ t('site.docs.eyebrow') }}
          </div>
          <h1>
            <span>{{ t('site.docs.title') }}</span>
            <strong>{{ t('site.docs.titleAccent') }}</strong>
          </h1>
          <p>{{ t('site.docs.description') }}</p>

          <label class="hero-search">
            <IconSearch :size="20" :stroke-width="1.8" />
            <input
              ref="heroSearchInput"
              v-model="searchText"
              type="search"
              :placeholder="t('site.docs.searchPlaceholder')"
            >
            <kbd>/</kbd>
          </label>

          <div class="hero-trust">
            <IconBook2 :size="15" :stroke-width="1.9" />
            {{ t('site.docs.preparing') }}
          </div>
        </div>

        <div class="docs-illustration" aria-hidden="true">
          <div class="illustration-glow" />
          <div class="document-layer document-layer-back" />
          <div class="document-layer document-layer-middle" />
          <div class="document-sheet">
            <div class="sheet-header">
              <img :src="pixelsLogo" alt="">
              <span>PIXELS / DOCS</span>
              <small>03</small>
            </div>
            <div class="sheet-body">
              <div
                v-for="product in productDefinitions"
                :key="product.key"
                class="sheet-product"
                :class="`tone-${product.tone}`"
              >
                <span class="sheet-icon">
                  <component :is="product.icon" :size="23" :stroke-width="1.55" />
                </span>
                <div>
                  <small>PRODUCT {{ product.number }}</small>
                  <strong>{{ t(`site.docs.products.${product.key}.title`) }}</strong>
                </div>
                <span class="sheet-lines"><i /><i /><i /></span>
              </div>
            </div>
          </div>
          <span class="floating-label label-one">INSTALL</span>
          <span class="floating-label label-two">CONFIGURE</span>
          <span class="floating-label label-three">OPERATE</span>
        </div>
      </div>
    </section>

    <section class="product-index">
      <div class="page-shell index-grid">
        <button
          v-for="product in productDefinitions"
          :key="product.key"
          type="button"
          :class="`tone-${product.tone}`"
          @click="scrollToProduct(product.key)"
        >
          <span class="index-number">{{ product.number }}</span>
          <span class="index-icon">
            <component :is="product.icon" :size="23" :stroke-width="1.6" />
          </span>
          <span class="index-copy">
            <strong>{{ productContent(product.key).title }}</strong>
            <small>{{ productContent(product.key).subtitle }}</small>
          </span>
          <IconArrowRight :size="18" :stroke-width="1.7" />
        </button>
      </div>
    </section>

    <section class="section docs-section">
      <div class="page-shell">
        <div class="section-heading">
          <span>{{ t('site.docs.chooseProduct') }}</span>
          <h2>{{ t('site.docs.browseTitle') }}</h2>
          <p>{{ t('site.docs.browseDescription') }}</p>
        </div>

        <div v-if="visibleProducts.length" class="docs-stack">
          <article
            v-for="(visibleProduct, productIndex) in visibleProducts"
            :id="`docs-${visibleProduct.definition.key}`"
            :key="visibleProduct.definition.key"
            class="docs-panel"
            :class="[
              `tone-${visibleProduct.definition.tone}`,
              { reversed: productIndex % 2 === 1 },
            ]"
          >
            <div class="docs-copy">
              <span class="product-number">PRODUCT {{ visibleProduct.definition.number }}</span>
              <div class="product-title">
                <span>
                  <component
                    :is="visibleProduct.definition.icon"
                    :size="29"
                    :stroke-width="1.55"
                  />
                </span>
                <div>
                  <small>{{ visibleProduct.content.subtitle }}</small>
                  <h3>{{ visibleProduct.content.title }}</h3>
                </div>
              </div>
              <p>{{ visibleProduct.content.description }}</p>
              <div class="quick-label">
                <IconRoute :size="17" :stroke-width="1.8" />
                {{ t('site.docs.quickStart') }}
              </div>
              <ol>
                <li
                  v-for="(quickStep, quickStepIndex) in visibleProduct.content.quickSteps"
                  :key="quickStep"
                >
                  <span>0{{ quickStepIndex + 1 }}</span>
                  {{ quickStep }}
                </li>
              </ol>
            </div>

            <div class="docs-visual">
              <div class="visual-grid" />
              <div class="visual-glow" />
              <div class="topic-sheet">
                <div class="topic-sheet-header">
                  <span>{{ t('site.docs.topics') }}</span>
                  <small>{{ visibleProduct.sections.length }} / 03</small>
                </div>
                <div class="topic-sections">
                  <section
                    v-for="documentationSection in visibleProduct.sections"
                    :key="documentationSection.title"
                  >
                    <div class="topic-title">
                      <span />
                      <div>
                        <strong>{{ documentationSection.title }}</strong>
                        <small>{{ documentationSection.description }}</small>
                      </div>
                    </div>
                    <ul>
                      <li
                        v-for="articleTitle in documentationSection.articles"
                        :key="articleTitle"
                      >
                        <span>{{ articleTitle }}</span>
                        <small>{{ t('site.docs.comingSoon') }}</small>
                        <IconChevronRight :size="15" :stroke-width="1.7" />
                      </li>
                    </ul>
                  </section>
                </div>
              </div>
            </div>
          </article>
        </div>

        <div v-else class="empty-state">
          <IconSearch :size="34" :stroke-width="1.5" />
          <strong>{{ t('site.docs.noResults') }}</strong>
          <button type="button" @click="searchText = ''">
            {{ t('site.docs.clearSearch') }}
          </button>
        </div>
      </div>
    </section>

    <section class="page-shell final-cta">
      <div class="cta-grid" aria-hidden="true" />
      <div class="cta-orb" aria-hidden="true" />
      <div class="cta-copy">
        <span>{{ t('site.docs.support.eyebrow') }}</span>
        <h2>{{ t('site.docs.support.title') }}</h2>
        <p>{{ t('site.docs.support.description') }}</p>
      </div>
      <button type="button" @click="contactVisible = true">
        {{ t('site.docs.support.action') }}
        <IconArrowUpRight :size="18" :stroke-width="1.9" />
      </button>
    </section>
  </div>
</template>

<style scoped>
.docs-page {
    --background: #fafafa;
    --foreground: #18181b;
    --card: #ffffff;
    --primary: #007f49;
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
.hero-copy > p { max-width: 590px; margin: 0; color: var(--muted-foreground); font-size: 16px; line-height: 1.85; }

.hero-search {
    display: grid;
    max-width: 590px;
    height: 54px;
    grid-template-columns: 34px 1fr auto;
    align-items: center;
    margin-top: 32px;
    padding: 0 14px;
    border: 1px solid var(--border);
    border-radius: 11px;
    background: var(--card);
    box-shadow: 0 14px 32px rgba(0, 127, 73, 0.08);
    color: var(--primary);
}

.hero-search input { min-width: 0; border: 0; outline: 0; background: transparent; color: var(--foreground); font: 500 13px var(--font-ui); }
.hero-search input::placeholder { color: var(--muted-foreground); }
.hero-search kbd { display: grid; width: 25px; height: 25px; place-items: center; border: 1px solid var(--border); border-radius: 6px; background: var(--secondary); color: var(--muted-foreground); font: 600 11px var(--font-tech); }
.hero-trust { display: flex; align-items: center; gap: 8px; margin-top: 24px; color: var(--muted-foreground); font: 700 11px var(--font-tech); letter-spacing: .08em; }
.hero-trust svg { color: var(--primary); }

.docs-illustration { position: relative; height: 520px; perspective: 1000px; }
.illustration-glow { position: absolute; inset: 12%; border-radius: 50%; background: radial-gradient(circle, rgba(0,154,89,.22), transparent 67%); filter: blur(20px); }
.document-layer,
.document-sheet { position: absolute; top: 50%; left: 50%; width: 390px; height: 390px; border: 1px solid rgba(0,127,73,.16); border-radius: 22px; }
.document-layer-back { background: linear-gradient(145deg, #dcfce7, #dbeafe); transform: translate(-44%, -47%) rotate(8deg); }
.document-layer-middle { background: linear-gradient(145deg, #ecfdf5, #ede9fe); transform: translate(-48%, -49%) rotate(3deg); }
.document-sheet { overflow: hidden; background: color-mix(in srgb, var(--card) 92%, transparent); box-shadow: 0 32px 70px rgba(0,63,36,.16); transform: translate(-52%, -51%) rotate(-3deg); backdrop-filter: blur(16px); animation: float-sheet 6s ease-in-out infinite; }
.sheet-header { display: grid; height: 58px; grid-template-columns: 32px 1fr auto; align-items: center; padding: 0 19px; border-bottom: 1px solid var(--border); color: var(--muted-foreground); }
.sheet-header img { width: 25px; height: 25px; }
.sheet-header span,
.sheet-header small { font: 700 9px var(--font-tech); letter-spacing: .1em; }
.sheet-body { display: grid; gap: 10px; padding: 22px; }
.sheet-product { display: grid; min-height: 82px; grid-template-columns: 52px 1fr 72px; align-items: center; gap: 13px; padding: 12px; border: 1px solid var(--border); border-radius: 14px; background: var(--card); }
.sheet-icon { display: grid; width: 48px; height: 48px; place-items: center; border-radius: 13px; background: var(--tone-soft); color: var(--tone); }
.sheet-product div { display: grid; gap: 5px; }
.sheet-product small { color: var(--tone); font: 700 8px var(--font-brand); }
.sheet-product strong { font-size: 13px; }
.sheet-lines { display: grid; gap: 6px; }
.sheet-lines i { height: 4px; border-radius: 4px; background: var(--border); }
.sheet-lines i:nth-child(2) { width: 76%; }
.sheet-lines i:nth-child(3) { width: 48%; }
.floating-label { position: absolute; padding: 8px 11px; border: 1px solid var(--border); border-radius: 9px; background: var(--card); box-shadow: 0 12px 28px rgba(24,24,27,.1); color: var(--primary); font: 700 8px var(--font-tech); letter-spacing: .1em; }
.label-one { top: 48px; right: 20px; }
.label-two { top: 50%; left: 0; }
.label-three { right: 6px; bottom: 52px; }

.tone-green { --tone: #008f52; --tone-soft: #e8f8ef; --tone-glow: rgba(0,154,89,.28); }
.tone-blue { --tone: #3976d8; --tone-soft: #edf4ff; --tone-glow: rgba(57,118,216,.3); }
.tone-violet { --tone: #7959c7; --tone-soft: #f2eefc; --tone-glow: rgba(121,89,199,.3); }

.product-index { border-bottom: 1px solid var(--border); background: var(--card); }
.index-grid { display: grid; grid-template-columns: repeat(3, 1fr); }
.index-grid button { display: grid; min-height: 112px; grid-template-columns: 28px 42px 1fr auto; align-items: center; gap: 13px; padding: 22px 32px; border: 0; border-right: 1px solid var(--border); background: transparent; color: var(--foreground); cursor: pointer; text-align: left; }
.index-grid button:first-child { padding-left: 0; }
.index-grid button:last-child { border-right: 0; }
.index-number { color: var(--tone); font: 700 11px var(--font-brand); }
.index-icon { display: grid; width: 42px; height: 42px; place-items: center; border-radius: 11px; background: var(--tone-soft); color: var(--tone); }
.index-copy { display: grid; gap: 4px; }
.index-copy strong { font-size: 14px; }
.index-copy small { color: var(--muted-foreground); font: 600 8px var(--font-tech); letter-spacing: .07em; }
.index-grid button > svg { color: var(--tone); transition: transform 160ms ease; }
.index-grid button:hover > svg { transform: translateX(4px); }

.section { padding: 120px 0; }
.section-heading h2 { max-width: 720px; margin: 17px 0 18px; font: 730 clamp(34px, 4vw, 52px) / 1.16 var(--font-ui); letter-spacing: -0.052em; }
.section-heading p { max-width: 650px; margin: 0; color: var(--muted-foreground); font-size: 15px; line-height: 1.8; }
.docs-stack { display: grid; gap: 22px; margin-top: 58px; }

.docs-panel { display: grid; min-height: 520px; grid-template-columns: .86fr 1.14fr; overflow: hidden; scroll-margin-top: 100px; border: 1px solid var(--border); border-radius: 22px; background: var(--card); box-shadow: 0 14px 45px rgba(24,24,27,.045); }
.docs-panel.reversed { grid-template-columns: 1.14fr .86fr; }
.docs-panel.reversed .docs-copy { order: 2; }
.docs-copy { display: flex; align-items: flex-start; justify-content: center; flex-direction: column; padding: 48px 52px; }
.product-number { color: var(--tone); font: 700 14px var(--font-brand); letter-spacing: .08em; }
.product-title { display: flex; align-items: center; gap: 15px; margin: 22px 0 15px; }
.product-title > span { display: grid; width: 52px; height: 52px; place-items: center; border-radius: 14px; background: var(--tone-soft); color: var(--tone); }
.product-title small { color: var(--tone); font: 700 9px var(--font-tech); letter-spacing: .08em; text-transform: uppercase; }
.product-title h3 { margin: 4px 0 0; font-size: 31px; letter-spacing: -.035em; }
.docs-copy > p { margin: 0; color: var(--muted-foreground); font-size: 14px; line-height: 1.78; }
.quick-label { display: flex; align-items: center; gap: 8px; margin-top: 25px; color: var(--tone); font-size: 12px; font-weight: 750; }
.docs-copy ol { display: grid; gap: 10px; margin: 15px 0 0; padding: 0; list-style: none; }
.docs-copy li { display: grid; grid-template-columns: 26px 1fr; align-items: center; gap: 9px; font-size: 12px; line-height: 1.5; }
.docs-copy li span { display: grid; width: 24px; height: 24px; place-items: center; border-radius: 8px; background: var(--tone-soft); color: var(--tone); font: 700 8px var(--font-brand); }

.docs-visual { position: relative; min-height: 520px; overflow: hidden; isolation: isolate; background: linear-gradient(145deg, var(--tone-soft), #eefbf5 52%, #eef3ff); }
.visual-grid { z-index: -1; background-size: 30px 30px; mask-image: linear-gradient(135deg, transparent 2%, black 24%, black 72%, transparent 96%); }
.visual-glow { position: absolute; right: 5%; bottom: -20%; left: 5%; height: 48%; border-radius: 50%; background: linear-gradient(90deg, var(--tone-glow), rgba(14,165,233,.22), rgba(168,85,247,.22)); filter: blur(42px); }
.topic-sheet { position: absolute; top: 8%; right: 7%; bottom: 8%; left: 7%; overflow: hidden; border: 1px solid rgba(255,255,255,.75); border-radius: 18px; background: color-mix(in srgb, var(--card) 94%, transparent); box-shadow: 0 28px 55px rgba(15,23,42,.16); backdrop-filter: blur(12px); transition: transform 320ms ease; }
.docs-panel:hover .topic-sheet { transform: translateY(-5px); }
.topic-sheet-header { display: flex; height: 42px; align-items: center; justify-content: space-between; padding: 0 16px; border-bottom: 1px solid var(--border); color: var(--tone); font: 700 9px var(--font-tech); letter-spacing: .09em; }
.topic-sections { display: grid; grid-template-columns: repeat(3, minmax(0,1fr)); height: calc(100% - 42px); }
.topic-sections section { min-width: 0; padding: 17px 12px; border-right: 1px solid var(--border); }
.topic-sections section:last-child { border-right: 0; }
.topic-title { display: grid; min-height: 88px; grid-template-columns: 8px 1fr; gap: 8px; }
.topic-title > span { width: 7px; height: 7px; margin-top: 4px; border-radius: 50%; background: var(--tone); box-shadow: 0 0 0 4px color-mix(in srgb, var(--tone) 12%, transparent); }
.topic-title div { min-width: 0; }
.topic-title strong { display: block; margin-bottom: 7px; font-size: 11px; }
.topic-title small { display: block; color: var(--muted-foreground); font-size: 9px; line-height: 1.45; }
.topic-sections ul { margin: 0; padding: 7px 0 0; border-top: 1px solid var(--border); list-style: none; }
.topic-sections li { display: grid; min-height: 52px; grid-template-columns: minmax(0,1fr) 15px; align-items: center; gap: 5px; padding: 6px 3px; border-bottom: 1px solid color-mix(in srgb, var(--border) 65%, transparent); }
.topic-sections li > span { overflow: hidden; font-size: 9px; font-weight: 650; line-height: 1.35; text-overflow: ellipsis; }
.topic-sections li small { display: none; }
.topic-sections li svg { color: var(--tone); }

.empty-state { display: grid; min-height: 360px; place-items: center; align-content: center; gap: 14px; margin-top: 58px; border: 1px dashed var(--border); border-radius: 20px; color: var(--muted-foreground); }
.empty-state strong { color: var(--foreground); }
.empty-state button { padding: 9px 14px; border: 1px solid var(--border); border-radius: 9px; background: var(--card); color: var(--primary); cursor: pointer; font: 700 12px var(--font-ui); }

.final-cta { position: relative; display: flex; min-height: 330px; align-items: center; justify-content: space-between; gap: 55px; margin-bottom: 120px; padding: 62px 68px; overflow: hidden; border-radius: 24px; background: linear-gradient(125deg, #052e22, #007f49 70%, #009a59); color: #fff; }
.cta-grid { opacity: .35; mask-image: linear-gradient(90deg, transparent, black); }
.cta-orb { position: absolute; right: -100px; width: 330px; height: 330px; border: 1px solid rgba(255,255,255,.2); border-radius: 50%; box-shadow: 0 0 0 55px rgba(255,255,255,.04), 0 0 0 110px rgba(255,255,255,.025); }
.cta-copy { position: relative; z-index: 1; max-width: 700px; }
.cta-copy > span { color: #8ceec0; }
.cta-copy h2 { margin: 17px 0 15px; font: 730 clamp(33px, 4vw, 50px) / 1.14 var(--font-ui); letter-spacing: -.05em; }
.cta-copy p { max-width: 600px; margin: 0; color: rgba(255,255,255,.72); font-size: 14px; line-height: 1.75; }
.final-cta > button { position: relative; z-index: 1; display: inline-flex; flex: 0 0 auto; min-height: 48px; align-items: center; gap: 10px; padding: 0 20px; border: 0; border-radius: 11px; background: #fff; color: #006b3d; cursor: pointer; font: 700 14px var(--font-ui); }

@keyframes float-sheet { 50% { transform: translate(-52%, -53%) rotate(-2deg); } }

:global(html[data-theme='dark'] .docs-page) {
    --background: #09090b;
    --foreground: #fafafa;
    --card: #101014;
    --primary: #009a59;
    --primary-bright: #20c77a;
    --primary-foreground: #fff;
    --secondary: #141416;
    --secondary-foreground: #f4f4f5;
    --muted-foreground: #a1a1aa;
    --accent: #052e22;
    --border: #27272a;
}

:global(html[data-theme='dark'] .document-layer-back) { background: linear-gradient(145deg, #0a3021, #10233e); }
:global(html[data-theme='dark'] .document-layer-middle) { background: linear-gradient(145deg, #0b3021, #271d40); }
:global(html[data-theme='dark'] .tone-green) { --tone-soft: #0a3021; }
:global(html[data-theme='dark'] .tone-blue) { --tone-soft: #10233e; }
:global(html[data-theme='dark'] .tone-violet) { --tone-soft: #271d40; }

@media (prefers-reduced-motion: reduce) {
    .document-sheet { animation: none; }
}

@media (max-width: 1020px) {
    .hero-layout { grid-template-columns: 1fr; gap: 20px; padding-top: 100px; }
    .docs-illustration { width: min(620px, 100%); justify-self: center; }
    .docs-copy { padding: 42px; }
    .topic-sections { grid-template-columns: 1fr; overflow: auto; }
    .topic-sections section { border-right: 0; border-bottom: 1px solid var(--border); }
    .topic-title { min-height: auto; margin-bottom: 12px; }
}

@media (max-width: 820px) {
    .page-shell { width: min(100% - 32px, 680px); }
    .index-grid { grid-template-columns: 1fr; }
    .index-grid button { min-height: 84px; padding: 18px 0; border-right: 0; border-bottom: 1px solid var(--border); }
    .index-grid button:last-child { border-bottom: 0; }
    .docs-panel,
    .docs-panel.reversed { grid-template-columns: 1fr; }
    .docs-panel.reversed .docs-copy { order: 0; }
    .docs-visual { min-height: 500px; }
    .topic-sections { grid-template-columns: repeat(3, minmax(0,1fr)); overflow: visible; }
    .topic-sections section { border-right: 1px solid var(--border); border-bottom: 0; }
    .final-cta { align-items: flex-start; flex-direction: column; margin-bottom: 90px; padding: 48px 38px; }
}

@media (max-width: 540px) {
    .hero-section,
    .hero-layout { min-height: auto; }
    .hero-copy h1 { font-size: 48px; }
    .docs-illustration { height: 430px; }
    .document-layer,
    .document-sheet { width: 300px; height: 350px; }
    .sheet-product { grid-template-columns: 44px 1fr; }
    .sheet-icon { width: 42px; height: 42px; }
    .sheet-lines { display: none; }
    .floating-label { display: none; }
    .section { padding: 90px 0; }
    .docs-copy { padding: 36px 26px; }
    .docs-visual { min-height: 620px; }
    .topic-sheet { top: 5%; right: 5%; bottom: 5%; left: 5%; }
    .topic-sections { grid-template-columns: 1fr; overflow: auto; }
    .topic-sections section { border-right: 0; border-bottom: 1px solid var(--border); }
    .final-cta { padding: 40px 26px; }
}
</style>
