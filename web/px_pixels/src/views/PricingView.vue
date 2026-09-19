<script setup lang="ts">
import { computed, ref, type Component } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'
import {
    IconArrowRight,
    IconArrowUpRight,
    IconBroadcast,
    IconCategory,
    IconCheck,
    IconChevronDown,
    IconDevices,
    IconHeadset,
    IconStack2,
} from '@tabler/icons-vue'
import ContactUs from '@/components/ContactUs.vue'
import pixelsLogo from '@/assets/pixels-logo-45.svg'

interface PricingPlan {
    name: string
    audience: string
    priceLabel: string
    priceNote: string
    features: string[]
}

interface PricingDimension {
    title: string
    description: string
}

interface PricingFaq {
    question: string
    answer: string
}

const { t, tm } = useI18n()
const router = useRouter()
const contactVisible = ref(false)

const dimensionIcons: Component[] = [
    IconCategory,
    IconBroadcast,
    IconDevices,
    IconHeadset,
]

const pricingPlans = computed(() => tm('site.pricing.plans') as PricingPlan[])
const pricingDimensions = computed(
    () => tm('site.pricing.dimensions') as PricingDimension[],
)
const comparisonHeadings = computed(
    () => tm('site.pricing.compareHeadings') as string[],
)
const comparisonRows = computed(
    () => tm('site.pricing.compareRows') as string[][],
)
const pricingFaqs = computed(() => tm('site.pricing.faqs') as PricingFaq[])

function viewPrivateDeployment() {
    void router.push('/about')
}
</script>

<template>
  <ContactUs v-model="contactVisible" />
  <div class="pricing-page">
    <section class="pricing-hero">
      <div class="hero-grid" aria-hidden="true" />
      <div class="hero-glow" aria-hidden="true" />
      <div class="page-shell hero-layout">
        <div class="hero-copy">
          <span class="hero-eyebrow">{{ t('site.pricing.eyebrow') }}</span>
          <h1>{{ t('site.pricing.title') }}</h1>
          <p>{{ t('site.pricing.description') }}</p>
          <span class="pricing-notice">
            <i />{{ t('site.pricing.notice') }}
          </span>
          <div class="hero-actions">
            <button class="button-primary" type="button" @click="contactVisible = true">
              {{ t('site.pricing.primaryAction') }}
              <IconArrowUpRight :size="17" :stroke-width="1.9" />
            </button>
            <button class="button-secondary" type="button" @click="viewPrivateDeployment">
              {{ t('site.pricing.secondaryAction') }}
              <IconArrowRight :size="17" :stroke-width="1.9" />
            </button>
          </div>
        </div>

        <div class="quote-visual" aria-hidden="true">
          <div class="quote-header">
            <span>PIXELS / LICENSE SCOPE</span>
            <img :src="pixelsLogo" alt="">
          </div>
          <div class="quote-body">
            <div
              v-for="(pricingDimension, dimensionIndex) in pricingDimensions"
              :key="pricingDimension.title"
              class="quote-row"
            >
              <span>0{{ dimensionIndex + 1 }}</span>
              <component
                :is="dimensionIcons[dimensionIndex]"
                :size="20"
                :stroke-width="1.6"
              />
              <strong>{{ pricingDimension.title }}</strong>
              <i :style="{ '--progress': `${48 + dimensionIndex * 14}%` }" />
            </div>
          </div>
          <div class="quote-footer">
            <span><i />PRIVATE DEPLOYMENT</span>
            <small>CONFIGURED FOR YOUR SCOPE</small>
          </div>
        </div>
      </div>
    </section>

    <section class="section plans-section">
      <div class="page-shell">
        <div class="section-heading centered-heading">
          <h2>{{ t('site.pricing.plansTitle') }}</h2>
          <p>{{ t('site.pricing.plansDescription') }}</p>
        </div>

        <div class="plans-grid">
          <article
            v-for="(pricingPlan, planIndex) in pricingPlans"
            :key="pricingPlan.name"
            class="plan-card"
          >
            <div class="plan-heading">
              <span>PRODUCT 0{{ planIndex + 1 }}</span>
              <h3>{{ pricingPlan.name }}</h3>
              <p>{{ pricingPlan.audience }}</p>
            </div>
            <div class="plan-price">
              <strong>{{ pricingPlan.priceLabel }}</strong>
              <span>{{ pricingPlan.priceNote }}</span>
            </div>
            <button type="button" @click="contactVisible = true">
              {{ t('site.pricing.quoteAction') }}
              <IconArrowUpRight :size="16" :stroke-width="1.8" />
            </button>
            <div class="plan-features">
              <span>{{ t('site.pricing.included') }}</span>
              <ul>
                <li v-for="feature in pricingPlan.features" :key="feature">
                  <i><IconCheck :size="11" :stroke-width="2.2" /></i>
                  {{ feature }}
                </li>
              </ul>
            </div>
          </article>
        </div>
      </div>
    </section>

    <section class="section dimensions-section">
      <div class="page-shell">
        <div class="section-heading dimensions-heading">
          <h2>{{ t('site.pricing.dimensionsTitle') }}</h2>
          <p>{{ t('site.pricing.dimensionsDescription') }}</p>
        </div>
        <div class="dimensions-grid">
          <article
            v-for="(pricingDimension, dimensionIndex) in pricingDimensions"
            :key="pricingDimension.title"
          >
            <span>0{{ dimensionIndex + 1 }}</span>
            <div>
              <component
                :is="dimensionIcons[dimensionIndex]"
                :size="24"
                :stroke-width="1.55"
              />
            </div>
            <h3>{{ pricingDimension.title }}</h3>
            <p>{{ pricingDimension.description }}</p>
          </article>
        </div>
      </div>
    </section>

    <section class="section comparison-section">
      <div class="page-shell">
        <div class="section-heading comparison-heading">
          <h2>{{ t('site.pricing.compareTitle') }}</h2>
        </div>
        <div class="comparison-table">
          <div class="comparison-header">
            <strong v-for="heading in comparisonHeadings" :key="heading">
              {{ heading }}
            </strong>
          </div>
          <div
            v-for="comparisonRow in comparisonRows"
            :key="comparisonRow[0]"
            class="comparison-row"
          >
            <strong>{{ comparisonRow[0] }}</strong>
            <span v-for="(cellValue, cellIndex) in comparisonRow.slice(1)" :key="cellIndex">
              <small>{{ comparisonHeadings[cellIndex + 1] }}</small>
              <IconCheck
                v-if="cellValue !== '—'"
                :size="15"
                :stroke-width="2"
              />
              {{ cellValue }}
            </span>
          </div>
        </div>
      </div>
    </section>

    <section class="section faq-section">
      <div class="page-shell faq-layout">
        <div class="section-heading faq-heading">
          <h2>{{ t('site.pricing.faqTitle') }}</h2>
          <IconStack2 :size="46" :stroke-width="1.2" />
        </div>
        <div class="faq-list">
          <details v-for="(pricingFaq, faqIndex) in pricingFaqs" :key="pricingFaq.question">
            <summary>
              <span>0{{ faqIndex + 1 }}</span>
              <strong>{{ pricingFaq.question }}</strong>
              <IconChevronDown :size="19" :stroke-width="1.7" />
            </summary>
            <p>{{ pricingFaq.answer }}</p>
          </details>
        </div>
      </div>
    </section>

    <section class="page-shell final-cta">
      <div class="cta-grid" aria-hidden="true" />
      <div class="cta-copy">
        <h2>{{ t('site.pricing.closingTitle') }}</h2>
        <p>{{ t('site.pricing.closingDescription') }}</p>
      </div>
      <button type="button" @click="contactVisible = true">
        {{ t('site.pricing.primaryAction') }}
        <IconArrowUpRight :size="18" :stroke-width="1.9" />
      </button>
    </section>
  </div>
</template>

<style scoped>
.pricing-page {
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

.pricing-hero {
    position: relative;
    min-height: 700px;
    overflow: hidden;
    border-bottom: 1px solid var(--border);
    background:
        radial-gradient(circle at 78% 30%, rgba(0, 154, 89, 0.13), transparent 31%),
        linear-gradient(180deg, var(--background), color-mix(in srgb, var(--accent) 48%, var(--background)));
}

.hero-grid,
.cta-grid {
    position: absolute;
    inset: 0;
    background-image:
        linear-gradient(rgba(0, 127, 73, 0.055) 1px, transparent 1px),
        linear-gradient(90deg, rgba(0, 127, 73, 0.055) 1px, transparent 1px);
    background-size: 38px 38px;
    mask-image: linear-gradient(90deg, transparent, black 25%, black 82%, transparent);
}

.hero-glow {
    position: absolute;
    top: 15%;
    right: 10%;
    width: 470px;
    height: 470px;
    border-radius: 50%;
    background: rgba(0, 154, 89, 0.12);
    filter: blur(76px);
}

.hero-layout {
    position: relative;
    display: grid;
    min-height: 700px;
    grid-template-columns: 1.02fr 0.98fr;
    align-items: center;
    gap: 84px;
}

.hero-copy {
    position: relative;
    z-index: 2;
}

.hero-eyebrow {
    color: var(--primary);
    font: 700 11px var(--font-tech);
    letter-spacing: 0.14em;
}

.hero-copy h1 {
    max-width: 740px;
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

.pricing-notice {
    display: inline-flex;
    align-items: center;
    gap: 9px;
    margin-top: 22px;
    color: var(--muted-foreground);
    font-size: 12px;
}

.pricing-notice i {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--primary-bright);
    box-shadow: 0 0 0 5px rgba(0, 154, 89, 0.12);
}

.hero-actions {
    display: flex;
    gap: 12px;
    margin-top: 30px;
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

.quote-visual {
    position: relative;
    overflow: hidden;
    border: 1px solid color-mix(in srgb, var(--primary) 22%, var(--border));
    border-radius: 22px;
    background: color-mix(in srgb, var(--card) 95%, transparent);
    box-shadow: 0 30px 75px rgba(15, 23, 42, 0.14);
    backdrop-filter: blur(20px);
}

.quote-header,
.quote-footer {
    display: flex;
    min-height: 54px;
    align-items: center;
    justify-content: space-between;
    padding: 0 20px;
    border-bottom: 1px solid var(--border);
    color: var(--muted-foreground);
    font: 700 9px var(--font-tech);
    letter-spacing: 0.1em;
}

.quote-header img {
    width: 25px;
    height: 25px;
}

.quote-body {
    padding: 12px;
}

.quote-row {
    display: grid;
    min-height: 76px;
    grid-template-columns: 25px 38px 1fr 88px;
    align-items: center;
    gap: 11px;
    padding: 0 12px;
    border-bottom: 1px solid var(--border);
}

.quote-row:last-child {
    border-bottom: 0;
}

.quote-row > span {
    color: var(--muted-foreground);
    font: 10px var(--font-tech);
}

.quote-row > svg {
    color: var(--primary);
}

.quote-row > strong {
    font-size: 13px;
}

.quote-row > i {
    position: relative;
    height: 5px;
    overflow: hidden;
    border-radius: 5px;
    background: var(--secondary);
}

.quote-row > i::after {
    position: absolute;
    inset: 0 auto 0 0;
    width: var(--progress);
    border-radius: inherit;
    background: linear-gradient(90deg, var(--primary), #34d399);
    content: '';
}

.quote-footer {
    border-top: 1px solid var(--border);
    border-bottom: 0;
}

.quote-footer > span {
    display: flex;
    align-items: center;
    gap: 8px;
    color: var(--primary);
}

.quote-footer i {
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
.cta-copy h2 {
    margin: 0 0 18px;
    font: 730 clamp(34px, 3.6vw, 50px) / 1.14 var(--font-ui);
    letter-spacing: -0.052em;
}

.section-heading p,
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

.plans-grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    align-items: stretch;
    gap: 18px;
}

.plan-card {
    position: relative;
    display: flex;
    min-width: 0;
    flex-direction: column;
    padding: 34px;
    border: 1px solid var(--border);
    border-radius: 20px;
    background: var(--card);
    box-shadow: 0 14px 42px rgba(15, 23, 42, 0.045);
}

.plan-card.featured {
    border-color: var(--primary);
    box-shadow: 0 24px 60px rgba(0, 127, 73, 0.13);
}

.recommended-badge {
    position: absolute;
    top: 18px;
    right: 18px;
    padding: 6px 9px;
    border-radius: 7px;
    background: var(--accent);
    color: var(--primary);
    font-size: 10px;
    font-weight: 750;
}

.plan-heading > span {
    color: var(--primary);
    font: 700 10px var(--font-tech);
    letter-spacing: 0.12em;
}

.plan-heading h3 {
    margin: 22px 0 10px;
    font-size: 28px;
    font-weight: 760;
}

.plan-heading p {
    min-height: 48px;
    margin: 0;
    color: var(--muted-foreground);
    font-size: 13px;
    line-height: 1.7;
}

.plan-price {
    display: grid;
    min-height: 112px;
    align-content: center;
    gap: 8px;
    margin: 26px -34px 0;
    padding: 0 34px;
    border-block: 1px solid var(--border);
    background: var(--secondary);
}

.plan-price strong {
    font-size: 23px;
    font-weight: 760;
}

.plan-price span {
    color: var(--muted-foreground);
    font-size: 12px;
}

.plan-card > button {
    display: inline-flex;
    min-height: 44px;
    align-items: center;
    justify-content: center;
    gap: 8px;
    margin-top: 24px;
    border: 1px solid var(--border);
    border-radius: 10px;
    background: var(--card);
    color: var(--foreground);
    cursor: pointer;
    font: 700 13px var(--font-ui);
}

.featured > button {
    border-color: var(--primary);
    background: var(--primary);
    color: var(--primary-foreground);
}

.plan-features {
    margin-top: 28px;
}

.plan-features > span {
    color: var(--muted-foreground);
    font-size: 11px;
    font-weight: 700;
}

.plan-features ul {
    display: grid;
    gap: 13px;
    margin: 18px 0 0;
    padding: 0;
    list-style: none;
}

.plan-features li {
    display: flex;
    align-items: flex-start;
    gap: 9px;
    color: var(--secondary-foreground);
    font-size: 13px;
    line-height: 1.55;
}

.plan-features li i {
    display: grid;
    width: 18px;
    height: 18px;
    flex: 0 0 auto;
    place-items: center;
    border-radius: 50%;
    background: var(--accent);
    color: var(--primary);
}

.dimensions-section {
    border-block: 1px solid var(--border);
    background: var(--secondary);
}

.dimensions-heading {
    max-width: 720px;
    margin-bottom: 50px;
}

.dimensions-grid {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 20px;
    background: var(--border);
    gap: 1px;
}

.dimensions-grid article {
    position: relative;
    min-height: 250px;
    padding: 30px;
    background: var(--card);
}

.dimensions-grid article > span {
    position: absolute;
    top: 30px;
    right: 30px;
    color: var(--muted-foreground);
    font: 10px var(--font-tech);
}

.dimensions-grid article > div {
    display: grid;
    width: 44px;
    height: 44px;
    place-items: center;
    border-radius: 11px;
    background: var(--accent);
    color: var(--primary);
}

.dimensions-grid h3 {
    margin: 42px 0 12px;
    font-size: 19px;
    font-weight: 750;
}

.dimensions-grid p {
    margin: 0;
    color: var(--muted-foreground);
    font-size: 13px;
    line-height: 1.75;
}

.comparison-heading {
    margin-bottom: 48px;
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
    grid-template-columns: 1.25fr repeat(3, 1fr);
    align-items: center;
}

.comparison-header {
    min-height: 62px;
    background: var(--foreground);
    color: var(--background);
}

.comparison-header > *,
.comparison-row > * {
    padding-inline: 24px;
}

.comparison-header strong {
    font-size: 12px;
}

.comparison-row {
    min-height: 78px;
    border-top: 1px solid var(--border);
}

.comparison-row > strong {
    font-size: 13px;
}

.comparison-row > span {
    display: flex;
    align-items: center;
    gap: 7px;
    color: var(--muted-foreground);
    font-size: 13px;
}

.comparison-row > span:last-child {
    color: var(--foreground);
    font-weight: 650;
}

.comparison-row svg {
    flex: 0 0 auto;
    color: var(--primary);
}

.comparison-row small {
    display: none;
}

.faq-section {
    border-top: 1px solid var(--border);
    background: var(--secondary);
}

.faq-layout {
    display: grid;
    grid-template-columns: 0.65fr 1.35fr;
    align-items: start;
    gap: 80px;
}

.faq-heading {
    position: sticky;
    top: 120px;
}

.faq-heading svg {
    margin-top: 28px;
    color: var(--primary);
}

.faq-list {
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 18px;
    background: var(--card);
}

.faq-list details {
    border-bottom: 1px solid var(--border);
}

.faq-list details:last-child {
    border-bottom: 0;
}

.faq-list summary {
    display: grid;
    min-height: 86px;
    grid-template-columns: 35px 1fr 24px;
    align-items: center;
    padding: 0 27px;
    cursor: pointer;
    list-style: none;
}

.faq-list summary::-webkit-details-marker {
    display: none;
}

.faq-list summary > span {
    color: var(--muted-foreground);
    font: 10px var(--font-tech);
}

.faq-list summary strong {
    font-size: 15px;
}

.faq-list summary svg {
    color: var(--muted-foreground);
    transition: transform 180ms ease;
}

.faq-list details[open] summary svg {
    transform: rotate(180deg);
}

.faq-list details > p {
    margin: -8px 27px 0 62px;
    padding-bottom: 28px;
    color: var(--muted-foreground);
    font-size: 13px;
    line-height: 1.8;
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
    max-width: 700px;
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

:global(html[data-theme='dark'] .pricing-page) {
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
        grid-template-columns: 1fr 0.82fr;
        gap: 44px;
    }

    .plans-grid {
        grid-template-columns: 1fr;
    }

    .plan-heading p {
        min-height: 0;
    }

    .dimensions-grid {
        grid-template-columns: 1fr 1fr;
    }
}

@media (max-width: 820px) {
    .page-shell {
        width: min(100% - 32px, 680px);
    }

    .pricing-hero,
    .hero-layout {
        min-height: auto;
    }

    .hero-layout {
        grid-template-columns: 1fr;
        padding-block: 82px 60px;
    }

    .quote-visual {
        width: min(100%, 540px);
        justify-self: center;
    }

    .section {
        padding-block: 84px;
    }

    .faq-layout {
        grid-template-columns: 1fr;
        gap: 42px;
    }

    .faq-heading {
        position: static;
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

    .quote-row {
        grid-template-columns: 23px 30px 1fr;
    }

    .quote-row > i {
        display: none;
    }

    .dimensions-grid {
        grid-template-columns: 1fr;
    }

    .dimensions-grid article {
        min-height: 220px;
    }

    .comparison-header {
        display: none;
    }

    .comparison-row {
        grid-template-columns: 1fr;
        gap: 16px;
        padding: 24px;
    }

    .comparison-header > *,
    .comparison-row > * {
        padding: 0;
    }

    .comparison-row > strong {
        font-size: 17px;
    }

    .comparison-row > span {
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

    .comparison-row > span:not(:last-child) {
        display: block;
    }

    .faq-list summary {
        grid-template-columns: 28px 1fr 20px;
        padding-inline: 20px;
    }

    .faq-list details > p {
        margin-right: 20px;
        margin-left: 48px;
    }

    .final-cta {
        padding: 40px 26px;
    }
}
</style>
