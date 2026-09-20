<script setup lang="ts">
import { computed, ref, type Component } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'
import {
    IconArrowRight,
    IconArrowUpRight,
    IconBuildingSkyscraper,
    IconCheck,
    IconDatabase,
    IconPlugConnected,
    IconServer2,
} from '@tabler/icons-vue'
import ContactUs from '@/components/ContactUs.vue'
import pixelsLogo from '@/assets/pixels-logo-45.svg'

interface Principle {
    title: string
    description: string
}

const { t, tm } = useI18n()
const router = useRouter()
const contactVisible = ref(false)

const principleIcons: Component[] = [
    IconBuildingSkyscraper,
    IconDatabase,
    IconServer2,
    IconPlugConnected,
]

const principles = computed(() => tm('site.about.principles') as Principle[])
const boundaryFlow = computed(() => tm('site.about.boundaryFlow') as string[])
const comparisonHeadings = computed(
    () => tm('site.about.comparisonHeadings') as string[],
)
const comparisonRows = computed(
    () => tm('site.about.comparisonRows') as string[][],
)

function viewSolutions() {
    void router.push({ path: '/main', hash: '#solutions' })
}
</script>

<template>
  <ContactUs v-model="contactVisible" />
  <div class="about-page">
    <section class="about-hero">
      <div class="hero-grid" aria-hidden="true" />
      <div class="hero-glow" aria-hidden="true" />
      <div class="page-shell hero-layout">
        <div class="hero-copy">
          <span class="hero-eyebrow">{{ t('site.about.eyebrow') }}</span>
          <h1>{{ t('site.about.title') }}</h1>
          <p>{{ t('site.about.description') }}</p>
          <div class="hero-actions">
            <button class="button-primary" type="button" @click="contactVisible = true">
              {{ t('site.about.primaryAction') }}
              <IconArrowUpRight :size="17" :stroke-width="1.9" />
            </button>
            <button class="button-secondary" type="button" @click="viewSolutions">
              {{ t('site.about.secondaryAction') }}
              <IconArrowRight :size="17" :stroke-width="1.9" />
            </button>
          </div>
        </div>

        <div class="private-cloud-visual" aria-hidden="true">
          <div class="cloud-frame">
            <div class="frame-header">
              <span>PRIVATE CLOUD</span>
              <small>CONTROLLED BOUNDARY</small>
            </div>
            <div class="frame-body">
              <span class="frame-orbit orbit-one" />
              <span class="frame-orbit orbit-two" />
              <div class="brand-core">
                <img :src="pixelsLogo" alt="">
                <strong>PIXELS</strong>
              </div>
              <span class="boundary-node node-users">
                <IconBuildingSkyscraper :size="21" :stroke-width="1.55" />
              </span>
              <span class="boundary-node node-data">
                <IconDatabase :size="21" :stroke-width="1.55" />
              </span>
              <span class="boundary-node node-compute">
                <IconServer2 :size="21" :stroke-width="1.55" />
              </span>
            </div>
            <div class="frame-footer">
              <span><i />CUSTOMER MANAGED</span>
              <small>01 / PRIVATE DEPLOYMENT</small>
            </div>
          </div>
        </div>
      </div>
    </section>

    <section class="section principles-section">
      <div class="page-shell">
        <div class="section-heading centered-heading">
          <h2>{{ t('site.about.principlesTitle') }}</h2>
          <p>{{ t('site.about.principlesDescription') }}</p>
        </div>
        <div class="principles-grid">
          <article v-for="(principle, principleIndex) in principles" :key="principle.title">
            <span class="principle-number">0{{ principleIndex + 1 }}</span>
            <span class="principle-icon">
              <component :is="principleIcons[principleIndex]" :size="25" :stroke-width="1.55" />
            </span>
            <h3>{{ principle.title }}</h3>
            <p>{{ principle.description }}</p>
          </article>
        </div>
      </div>
    </section>

    <section class="page-shell boundary-section">
      <div class="boundary-grid" aria-hidden="true" />
      <div class="boundary-copy">
        <span>{{ t('site.about.boundaryEyebrow') }}</span>
        <h2>{{ t('site.about.boundaryTitle') }}</h2>
        <p>{{ t('site.about.boundaryDescription') }}</p>
      </div>
      <div class="boundary-flow">
        <template v-for="(flowLabel, flowIndex) in boundaryFlow" :key="flowLabel">
          <article>
            <span>0{{ flowIndex + 1 }}</span>
            <component
              :is="principleIcons[flowIndex]"
              :size="25"
              :stroke-width="1.5"
            />
            <strong>{{ flowLabel }}</strong>
          </article>
          <IconArrowRight
            v-if="flowIndex < boundaryFlow.length - 1"
            class="flow-arrow"
            :size="24"
            :stroke-width="1.4"
          />
        </template>
      </div>
    </section>

    <section class="section comparison-section">
      <div class="page-shell">
        <div class="section-heading comparison-heading">
          <h2>{{ t('site.about.comparisonTitle') }}</h2>
          <p>{{ t('site.about.comparisonDescription') }}</p>
        </div>
        <div class="comparison-table">
          <div class="comparison-header">
            <strong v-for="heading in comparisonHeadings" :key="heading">
              {{ heading }}
            </strong>
          </div>
          <article
            v-for="comparisonRow in comparisonRows"
            :key="comparisonRow[0]"
            class="comparison-row"
          >
            <strong>{{ comparisonRow[0] }}</strong>
            <div>
              <small>{{ comparisonHeadings[1] }}</small>
              <span>{{ comparisonRow[1] }}</span>
            </div>
            <div class="pixels-value">
              <small>{{ comparisonHeadings[2] }}</small>
              <IconCheck :size="17" :stroke-width="2" />
              <span>{{ comparisonRow[2] }}</span>
            </div>
          </article>
        </div>
      </div>
    </section>

    <section class="page-shell final-cta">
      <div class="cta-grid" aria-hidden="true" />
      <div class="cta-copy">
        <h2>{{ t('site.about.closingTitle') }}</h2>
        <p>{{ t('site.about.closingDescription') }}</p>
      </div>
      <button type="button" @click="contactVisible = true">
        {{ t('site.about.primaryAction') }}
        <IconArrowUpRight :size="18" :stroke-width="1.9" />
      </button>
    </section>
  </div>
</template>

<style scoped>
.about-page {
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

.page-shell {
    width: min(1200px, calc(100% - 48px));
    margin: 0 auto;
}

.about-hero {
    position: relative;
    min-height: 700px;
    overflow: hidden;
    border-bottom: 1px solid var(--border);
    background:
        radial-gradient(circle at 78% 32%, rgba(0, 154, 89, 0.13), transparent 31%),
        linear-gradient(180deg, var(--background), color-mix(in srgb, var(--accent) 48%, var(--background)));
}

.hero-grid,
.boundary-grid,
.cta-grid {
    position: absolute;
    inset: 0;
    background-image:
        linear-gradient(rgba(0, 127, 73, 0.055) 1px, transparent 1px),
        linear-gradient(90deg, rgba(0, 127, 73, 0.055) 1px, transparent 1px);
    background-size: 38px 38px;
    mask-image: linear-gradient(90deg, transparent, black 25%, black 80%, transparent);
}

.hero-glow {
    position: absolute;
    top: 16%;
    right: 11%;
    width: 460px;
    height: 460px;
    border-radius: 50%;
    background: rgba(0, 154, 89, 0.12);
    filter: blur(76px);
}

.hero-layout {
    position: relative;
    display: grid;
    min-height: 700px;
    grid-template-columns: 1.04fr 0.96fr;
    align-items: center;
    gap: 80px;
}

.hero-copy {
    position: relative;
    z-index: 2;
}

.hero-eyebrow,
.boundary-copy > span {
    color: var(--primary);
    font: 700 11px var(--font-tech);
    letter-spacing: 0.14em;
}

.hero-copy h1 {
    max-width: 720px;
    margin: 24px 0 22px;
    font: 750 clamp(46px, 4.5vw, 68px) / 1.07 var(--font-ui);
    letter-spacing: -0.062em;
}

.hero-copy > p {
    max-width: 650px;
    margin: 0;
    color: var(--muted-foreground);
    font-size: 15px;
    line-height: 1.85;
}

.hero-actions {
    display: flex;
    gap: 12px;
    margin-top: 32px;
}

.hero-actions button,
.final-cta > button {
    display: inline-flex;
    min-height: 46px;
    align-items: center;
    justify-content: center;
    gap: 9px;
    padding: 0 19px;
    border-radius: 10px;
    cursor: pointer;
    font: 700 13px var(--font-ui);
}

.button-primary {
    border: 1px solid var(--primary);
    background: var(--primary);
    color: var(--primary-foreground);
}

.button-secondary {
    border: 1px solid var(--border);
    background: var(--card);
    color: var(--foreground);
}

.private-cloud-visual {
    position: relative;
    width: min(100%, 490px);
    justify-self: end;
}

.cloud-frame {
    overflow: hidden;
    border: 1px solid color-mix(in srgb, var(--primary) 22%, var(--border));
    border-radius: 24px;
    background: color-mix(in srgb, var(--card) 94%, transparent);
    box-shadow: 0 32px 80px rgba(15, 23, 42, 0.14);
    backdrop-filter: blur(20px);
}

.frame-header,
.frame-footer {
    display: flex;
    min-height: 50px;
    align-items: center;
    justify-content: space-between;
    padding: 0 20px;
    border-bottom: 1px solid var(--border);
    color: var(--muted-foreground);
    font: 700 9px var(--font-tech);
    letter-spacing: 0.1em;
}

.frame-header span {
    color: var(--primary);
}

.frame-body {
    position: relative;
    min-height: 370px;
}

.frame-orbit {
    position: absolute;
    top: 50%;
    left: 50%;
    border: 1px solid rgba(0, 154, 89, 0.18);
    border-radius: 50%;
    transform: translate(-50%, -50%);
}

.orbit-one {
    width: 290px;
    height: 290px;
}

.orbit-two {
    width: 210px;
    height: 210px;
    border-style: dashed;
}

.brand-core {
    position: absolute;
    top: 50%;
    left: 50%;
    display: grid;
    width: 126px;
    height: 126px;
    place-items: center;
    border: 1px solid var(--border);
    border-radius: 28px;
    background: var(--card);
    box-shadow: 0 22px 50px rgba(15, 23, 42, 0.13);
    transform: translate(-50%, -50%) rotate(45deg);
}

.brand-core img,
.brand-core strong {
    position: absolute;
    transform: rotate(-45deg);
}

.brand-core img {
    top: 23px;
    width: 34px;
    height: 34px;
}

.brand-core strong {
    bottom: 25px;
    font: 700 13px var(--font-brand);
    letter-spacing: 0.1em;
}

.boundary-node {
    position: absolute;
    display: grid;
    width: 48px;
    height: 48px;
    place-items: center;
    border: 1px solid var(--border);
    border-radius: 14px;
    background: var(--card);
    box-shadow: 0 14px 28px rgba(15, 23, 42, 0.1);
    color: var(--primary);
}

.node-users {
    top: 54px;
    left: 60px;
}

.node-data {
    top: 92px;
    right: 48px;
}

.node-compute {
    right: 88px;
    bottom: 54px;
}

.frame-footer {
    border-top: 1px solid var(--border);
    border-bottom: 0;
}

.frame-footer span {
    display: flex;
    align-items: center;
    gap: 8px;
    color: var(--primary);
}

.frame-footer i {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--primary-bright);
    box-shadow: 0 0 0 4px rgba(0, 154, 89, 0.12);
}

.section {
    padding: 120px 0;
}

.section-heading h2,
.boundary-copy h2,
.cta-copy h2 {
    margin: 0 0 18px;
    font: 730 clamp(34px, 3.6vw, 50px) / 1.14 var(--font-ui);
    letter-spacing: -0.052em;
}

.section-heading p,
.boundary-copy p,
.cta-copy p {
    margin: 0;
    color: var(--muted-foreground);
    font-size: 14px;
    line-height: 1.8;
}

.centered-heading {
    max-width: 780px;
    margin: 0 auto 54px;
    text-align: center;
}

.principles-grid {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 20px;
    background: var(--border);
    gap: 1px;
}

.principles-grid article {
    position: relative;
    min-height: 270px;
    padding: 31px;
    background: var(--card);
}

.principle-number {
    position: absolute;
    top: 31px;
    right: 31px;
    color: var(--muted-foreground);
    font: 10px var(--font-tech);
}

.principle-icon {
    display: grid;
    width: 46px;
    height: 46px;
    place-items: center;
    border-radius: 12px;
    background: var(--accent);
    color: var(--primary);
}

.principles-grid h3 {
    margin: 48px 0 12px;
    font-size: 21px;
    font-weight: 750;
}

.principles-grid p {
    margin: 0;
    color: var(--muted-foreground);
    font-size: 13px;
    line-height: 1.75;
}

.boundary-section {
    position: relative;
    display: grid;
    min-height: 440px;
    grid-template-columns: 0.85fr 1.15fr;
    align-items: center;
    gap: 72px;
    margin-bottom: 120px;
    padding: 64px 68px;
    overflow: hidden;
    border-radius: 24px;
    background: linear-gradient(125deg, #052e22, #007f49 72%, #009a59);
    color: #ffffff;
}

.boundary-grid {
    opacity: 0.2;
}

.boundary-copy,
.boundary-flow {
    position: relative;
    z-index: 1;
}

.boundary-copy > span {
    color: #8ceec0;
}

.boundary-copy h2 {
    margin-top: 18px;
}

.boundary-copy p {
    color: rgba(255, 255, 255, 0.7);
}

.boundary-flow {
    display: grid;
    grid-template-columns: 1fr 36px 1fr 36px 1fr;
    align-items: center;
}

.boundary-flow article {
    display: grid;
    min-height: 154px;
    place-items: center;
    padding: 20px 12px;
    border: 1px solid rgba(255, 255, 255, 0.18);
    border-radius: 16px;
    background: rgba(255, 255, 255, 0.09);
    text-align: center;
    backdrop-filter: blur(10px);
}

.boundary-flow article span {
    color: rgba(255, 255, 255, 0.55);
    font: 10px var(--font-tech);
}

.boundary-flow article strong {
    font-size: 13px;
}

.flow-arrow {
    justify-self: center;
    color: rgba(255, 255, 255, 0.6);
}

.comparison-section {
    border-block: 1px solid var(--border);
    background: var(--secondary);
}

.comparison-heading {
    max-width: 760px;
    margin-bottom: 50px;
}

.comparison-table {
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 20px;
    background: var(--card);
}

.comparison-header,
.comparison-row {
    display: grid;
    grid-template-columns: 0.7fr 1fr 1.2fr;
    align-items: center;
}

.comparison-header {
    min-height: 62px;
    background: var(--foreground);
    color: var(--background);
}

.comparison-header strong,
.comparison-row > strong,
.comparison-row > div {
    padding-inline: 25px;
}

.comparison-header strong {
    font-size: 12px;
    letter-spacing: 0.04em;
}

.comparison-row {
    min-height: 94px;
    border-top: 1px solid var(--border);
}

.comparison-row > strong {
    font-size: 14px;
}

.comparison-row > div {
    display: flex;
    align-items: center;
    gap: 9px;
    color: var(--muted-foreground);
    font-size: 13px;
    line-height: 1.6;
}

.comparison-row small {
    display: none;
}

.comparison-row .pixels-value {
    color: var(--foreground);
    font-weight: 650;
}

.pixels-value svg {
    flex: 0 0 auto;
    color: var(--primary);
}

.final-cta {
    position: relative;
    display: flex;
    min-height: 330px;
    align-items: center;
    justify-content: space-between;
    gap: 55px;
    margin-block: 120px;
    padding: 62px 68px;
    overflow: hidden;
    border-radius: 24px;
    background: linear-gradient(125deg, #052e22, #007f49 70%, #009a59);
    color: #ffffff;
}

.cta-copy,
.final-cta > button {
    position: relative;
    z-index: 1;
}

.cta-copy {
    max-width: 690px;
}

.cta-copy p {
    color: rgba(255, 255, 255, 0.7);
}

.final-cta > button {
    flex: 0 0 auto;
    border: 0;
    background: #ffffff;
    color: #006b3d;
}

:global(html[data-theme='dark'] .about-page) {
    --background: #09090b;
    --foreground: #fafafa;
    --card: #101014;
    --primary: #009a59;
    --primary-bright: #009a59;
    --primary-foreground: #ffffff;
    --secondary: #18181b;
    --secondary-foreground: #fafafa;
    --muted-foreground: #a1a1aa;
    --accent: #052e22;
    --border: #27272a;
}

@media (max-width: 1020px) {
    .hero-layout {
        grid-template-columns: 1fr 0.8fr;
        gap: 42px;
    }

    .principles-grid {
        grid-template-columns: 1fr 1fr;
    }

    .boundary-section {
        grid-template-columns: 1fr;
    }
}

@media (max-width: 820px) {
    .page-shell {
        width: min(100% - 32px, 680px);
    }

    .about-hero,
    .hero-layout {
        min-height: auto;
    }

    .hero-layout {
        grid-template-columns: 1fr;
        padding-block: 82px 60px;
    }

    .private-cloud-visual {
        width: min(100%, 500px);
        justify-self: center;
    }

    .section {
        padding-block: 84px;
    }

    .boundary-section {
        margin-bottom: 84px;
        padding: 48px 38px;
    }

    .final-cta {
        align-items: flex-start;
        flex-direction: column;
        margin-block: 84px;
        padding: 48px 38px;
    }
}

@media (max-width: 560px) {
    .hero-copy h1 {
        font-size: 42px;
    }

    .hero-actions {
        align-items: stretch;
        flex-direction: column;
    }

    .frame-body {
        min-height: 300px;
    }

    .orbit-one {
        width: 240px;
        height: 240px;
    }

    .orbit-two {
        width: 170px;
        height: 170px;
    }

    .principles-grid {
        grid-template-columns: 1fr;
    }

    .principles-grid article {
        min-height: 220px;
    }

    .boundary-section {
        padding: 40px 26px;
    }

    .boundary-flow {
        grid-template-columns: 1fr;
        gap: 12px;
    }

    .boundary-flow article {
        min-height: 122px;
    }

    .flow-arrow {
        transform: rotate(90deg);
    }

    .comparison-header {
        display: none;
    }

    .comparison-row {
        grid-template-columns: 1fr;
        gap: 18px;
        padding: 24px;
    }

    .comparison-header strong,
    .comparison-row > strong,
    .comparison-row > div {
        padding: 0;
    }

    .comparison-row > strong {
        font-size: 17px;
    }

    .comparison-row > div {
        display: grid;
        grid-template-columns: 18px 1fr;
    }

    .comparison-row small {
        display: block;
        grid-column: 1 / -1;
        color: var(--muted-foreground);
        font-size: 10px;
        font-weight: 700;
        letter-spacing: 0.08em;
        text-transform: uppercase;
    }

    .comparison-row > div:not(.pixels-value) span {
        grid-column: 1 / -1;
    }

    .final-cta {
        padding: 40px 26px;
    }
}
</style>
