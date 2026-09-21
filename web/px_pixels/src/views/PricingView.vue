<script setup lang="ts">
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'
import {
    IconArrowUpRight,
    IconCheck,
    IconChevronDown,
    IconDeviceGamepad2,
    IconLayersLinked,
    IconSettings,
    IconStack2,
    IconX,
} from '@tabler/icons-vue'
import ContactUs from '@/components/ContactUs.vue'
import PricingCalculatorView from '@/views/PricingCalculatorView.vue'
import RemoteOemCalculator from '@/views/RemoteOemCalculator.vue'
import pixelsLogo from '@/assets/pixels-logo-45.svg'

type PersonalEdition = 'commercial' | 'free'
type PricingPage = 'remote' | 'cloud'

interface RemotePricingPlan {
    name: string
    audience: string
    streams: string
    price: string
    note: string
    features: string[]
    featured?: boolean
    enterprise?: boolean
    plus?: boolean
}

interface CloudProduct {
    name: string
    label: string
    description: string
    features: string[]
}

interface PricingFaq {
    question: string
    answer: string
}

const { t, tm } = useI18n()
const router = useRouter()
const selectedPersonalEdition = ref<PersonalEdition>('commercial')
const activePricingPage = ref<PricingPage>('remote')
const calculatorVisible = ref(false)
const remoteOemCalculatorVisible = ref(false)
const contactVisible = ref(false)
const contactTitle = ref('')
const contactContent = ref('')

const remotePlans = computed(
    () => tm('site.pricing.plans') as RemotePricingPlan[],
)
const freePersonalPlan = computed(
    () => tm('site.pricing.freePersonalPlan') as RemotePricingPlan,
)
const displayedRemotePlans = computed(() =>
    remotePlans.value.map((pricingPlan, planIndex) => {
        if (planIndex === 0 && selectedPersonalEdition.value === 'free') {
            return freePersonalPlan.value
        }

        return pricingPlan
    }),
)
const cloudProducts = computed(
    () => tm('site.pricing.cloudProducts') as CloudProduct[],
)
const pricingPageOptions: PricingPage[] = ['remote', 'cloud']
const pricingFaqs = computed(
    () =>
        tm(
            activePricingPage.value === 'remote'
                ? 'site.pricing.remoteFaqs'
                : 'site.pricing.cloudFaqs',
        ) as PricingFaq[],
)
const pricingFaqTitle = computed(() =>
    t(
        activePricingPage.value === 'remote'
            ? 'site.pricing.remoteFaqTitle'
            : 'site.pricing.cloudFaqTitle',
    ),
)

function openCalculator() {
    calculatorVisible.value = true
}

function openRemoteOemCalculator() {
    remoteOemCalculatorVisible.value = true
}

function consultRemotePlan(plan: RemotePricingPlan, planIndex: number) {
    if (planIndex === 0 && selectedPersonalEdition.value === 'free') {
        void router.push('/downloads')
        return
    }

    const planLabel =
        planIndex === 0
            ? `${plan.name} · ${t('site.pricing.commercialEdition')}`
            : plan.name
    contactTitle.value = t('site.pricing.contactTitle')
    contactContent.value = [
        `${t('site.pricing.contactPlan')}: ${planLabel}`,
        `${t('site.pricing.contactCapacity')}: ${plan.streams}`,
        `${t('site.pricing.contactLicense')}: ${t('site.pricing.annualBilling')}`,
        `${t('site.pricing.contactPrice')}: ${plan.price}`,
    ].join('\n')
    contactVisible.value = true
}
</script>

<template>
  <ContactUs
    v-model="contactVisible"
    :initial-title="contactTitle"
    :initial-content="contactContent"
    initial-consult-type="enterprise"
  />

  <el-dialog
    v-model="calculatorVisible"
    align-center
    append-to-body
    destroy-on-close
    :close-on-click-modal="false"
    :close-on-press-escape="false"
    :show-close="false"
    class="pixels-calculator-dialog"
    modal-class="pixels-calculator-overlay"
  >
    <template #header>
      <div class="calculator-dialog-header">
        <div>
          <img :src="pixelsLogo" alt="">
          <span>{{ t('pricingCalculator.title') }}</span>
        </div>
        <button
          type="button"
          :aria-label="t('consult.cancel')"
          @click="calculatorVisible = false"
        >
          <IconX :size="21" :stroke-width="1.8" />
        </button>
      </div>
    </template>
    <PricingCalculatorView embedded />
  </el-dialog>

  <el-dialog
    v-model="remoteOemCalculatorVisible"
    align-center
    append-to-body
    destroy-on-close
    :close-on-click-modal="false"
    :close-on-press-escape="false"
    :show-close="false"
    class="pixels-calculator-dialog pixels-remote-oem-dialog"
    modal-class="pixels-calculator-overlay"
  >
    <template #header>
      <div class="calculator-dialog-header">
        <div>
          <img :src="pixelsLogo" alt="">
          <span>{{ t('remoteOemCalculator.title') }}</span>
        </div>
        <button
          type="button"
          :aria-label="t('consult.cancel')"
          @click="remoteOemCalculatorVisible = false"
        >
          <IconX :size="21" :stroke-width="1.8" />
        </button>
      </div>
    </template>
    <RemoteOemCalculator />
  </el-dialog>

  <div class="pricing-page">
    <div class="deployment-notice-wrap" role="note">
      <strong class="plan-deployment-notice">
        {{ t('site.pricing.deploymentNotice') }}
      </strong>
    </div>

    <div class="page-shell pricing-page-switch-wrap">
      <nav
        class="pricing-page-switcher"
        :aria-label="t('site.pricing.pageSwitchLabel')"
      >
        <button
          v-for="pricingPageOption in pricingPageOptions"
          :key="pricingPageOption"
          type="button"
          :class="{ active: activePricingPage === pricingPageOption }"
          :aria-current="activePricingPage === pricingPageOption ? 'page' : undefined"
          @click="activePricingPage = pricingPageOption"
        >
          {{ t(`site.pricing.pageSwitch.${pricingPageOption}`) }}
        </button>
      </nav>
    </div>

    <section
      v-if="activePricingPage === 'remote'"
      id="remote-plans"
      class="section plans-section"
    >
      <div class="page-shell">
        <div class="section-heading centered-heading">
          <h2>{{ t('site.pricing.plansTitle') }}</h2>
          <p>{{ t('site.pricing.plansDescription') }}</p>
        </div>

        <div class="plans-grid">
          <article
            v-for="(pricingPlan, planIndex) in displayedRemotePlans"
            :key="`${planIndex}-${pricingPlan.name}`"
            class="plan-card"
            :class="{
              featured: pricingPlan.featured,
              enterprise: pricingPlan.enterprise,
              plus: pricingPlan.plus,
            }"
          >
            <span v-if="pricingPlan.featured" class="recommended-badge">
              {{ t('site.pricing.recommended') }}
            </span>
            <span v-else-if="pricingPlan.enterprise" class="recommended-badge">
              {{ t('site.pricing.enterpriseBadge') }}
            </span>
            <span v-else-if="pricingPlan.plus" class="recommended-badge">
              {{ t('site.pricing.plusBadge') }}
            </span>
            <div class="plan-heading">
              <span>REMOTE 0{{ planIndex + 1 }}</span>
              <h3>{{ pricingPlan.name }}</h3>
              <p>{{ pricingPlan.audience }}</p>
            </div>
            <div class="plan-edition-slot">
              <div
                v-if="planIndex === 0"
                class="personal-edition-toggle"
                role="group"
                :aria-label="t('site.pricing.personalEditionLabel')"
              >
                <button
                  type="button"
                  :class="{ active: selectedPersonalEdition === 'commercial' }"
                  :aria-pressed="selectedPersonalEdition === 'commercial'"
                  @click="selectedPersonalEdition = 'commercial'"
                >
                  {{ t('site.pricing.commercialEdition') }}
                </button>
                <button
                  type="button"
                  :class="{ active: selectedPersonalEdition === 'free' }"
                  :aria-pressed="selectedPersonalEdition === 'free'"
                  @click="selectedPersonalEdition = 'free'"
                >
                  {{ t('site.pricing.freeEdition') }}
                </button>
              </div>
            </div>
            <div class="plan-price">
              <small>{{ pricingPlan.streams }}</small>
              <strong>{{ pricingPlan.price }}</strong>
              <span>{{ pricingPlan.note }}</span>
            </div>
            <button type="button" @click="consultRemotePlan(pricingPlan, planIndex)">
              {{ planIndex === 0 && selectedPersonalEdition === 'free'
                ? t('site.pricing.freeAction')
                : t('site.pricing.quoteAction') }}
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

    <section v-else class="section cloud-pricing-section">
      <div class="page-shell">
        <div class="section-heading cloud-heading">
          <h2>{{ t('site.pricing.cloudTitle') }}</h2>
          <p>{{ t('site.pricing.cloudDescription') }}</p>
        </div>
        <div class="cloud-products-grid">
          <article
            v-for="(cloudProduct, productIndex) in cloudProducts"
            :key="cloudProduct.name"
          >
            <div class="cloud-product-icon">
              <IconDeviceGamepad2
                v-if="productIndex === 0"
                :size="29"
                :stroke-width="1.45"
              />
              <IconLayersLinked
                v-else-if="productIndex === 1"
                :size="29"
                :stroke-width="1.45"
              />
              <IconSettings v-else :size="29" :stroke-width="1.45" />
            </div>
            <div class="cloud-product-copy">
              <span>{{ cloudProduct.label }}</span>
              <h3>{{ cloudProduct.name }}</h3>
              <p>{{ cloudProduct.description }}</p>
              <ul>
                <li v-for="feature in cloudProduct.features" :key="feature">
                  <IconCheck :size="14" :stroke-width="2" />{{ feature }}
                </li>
              </ul>
            </div>
            <button
              type="button"
              @click="productIndex === 2 ? openRemoteOemCalculator() : openCalculator()"
            >
              {{ productIndex === 2
                ? t('site.pricing.remoteOemAction')
                : t('site.pricing.cloudAction') }}
              <IconArrowUpRight :size="17" :stroke-width="1.8" />
            </button>
          </article>
        </div>
      </div>
    </section>

    <section class="section faq-section">
      <div class="page-shell faq-layout">
        <div class="section-heading faq-heading">
          <h2>{{ pricingFaqTitle }}</h2>
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

.calculator-dialog-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 24px;
}

.calculator-dialog-header > div {
    display: flex;
    align-items: center;
    gap: 11px;
}

.calculator-dialog-header img {
    width: 27px;
    height: 27px;
}

.calculator-dialog-header span {
    color: var(--text);
    font-size: 16px;
    font-weight: 750;
}

.calculator-dialog-header > button {
    display: grid;
    width: 36px;
    height: 36px;
    flex: 0 0 auto;
    place-items: center;
    border: 0;
    border-radius: 9px;
    background: transparent;
    color: var(--muted);
    cursor: pointer;
}

.calculator-dialog-header > button:hover {
    background: var(--bg2);
    color: var(--text);
}

:global(.pixels-calculator-dialog) {
    display: flex;
    width: min(1500px, 96vw) !important;
    height: min(900px, calc(100vh - 48px));
    max-height: calc(100vh - 48px);
    flex-direction: column;
    margin: 0 !important;
    padding: 0 !important;
    overflow: hidden;
    border: 1px solid var(--line) !important;
    border-radius: 20px !important;
    background: var(--panel) !important;
    box-shadow: 0 32px 100px rgba(9, 9, 11, 0.28) !important;
}

:global(.pixels-calculator-dialog .el-dialog__header) {
    flex: 0 0 auto;
    margin: 0;
    padding: 15px 18px;
    border-bottom: 1px solid var(--line);
}

:global(.pixels-calculator-dialog .el-dialog__body) {
    min-height: 0;
    flex: 1 1 auto;
    padding: 0;
    overflow: hidden;
}

:global(.pixels-calculator-overlay) {
    padding: 0;
    overflow: hidden;
}

:global(.pixels-calculator-overlay .el-overlay-dialog) {
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 24px;
    overflow: hidden;
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

.hero-grid {
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

.hero-actions button {
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

.section-heading h2 {
    margin: 0 0 18px;
    font: 730 clamp(34px, 3.6vw, 50px) / 1.14 var(--font-ui);
    letter-spacing: -0.052em;
}

.section-heading p {
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

.deployment-notice-wrap {
    padding: 56px 24px 0;
    text-align: center;
}

.pricing-page-switch-wrap {
    padding-top: 34px;
}

.pricing-page-switcher {
    display: grid;
    width: min(720px, 100%);
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 6px;
    margin: 0 auto;
    padding: 6px;
    border: 1px solid var(--border);
    border-radius: 18px;
    background: var(--card);
    box-shadow: 0 16px 42px rgba(15, 23, 42, 0.06);
}

.pricing-page-switcher button {
    display: flex;
    min-width: 0;
    min-height: 54px;
    align-items: center;
    justify-content: center;
    padding: 10px 20px;
    border: 0;
    border-radius: 13px;
    background: transparent;
    color: var(--foreground);
    cursor: pointer;
    font-size: 15px;
    font-weight: 760;
    text-align: center;
    transition:
        background 180ms ease,
        color 180ms ease,
        transform 180ms ease;
}

.pricing-page-switcher button:hover {
    background: var(--secondary);
}

.pricing-page-switcher button.active {
    background: var(--primary);
    box-shadow: 0 9px 24px rgba(0, 127, 73, 0.18);
    color: var(--primary-foreground);
}

.plan-deployment-notice {
    position: relative;
    display: block;
    width: fit-content;
    margin: 0 auto;
    padding: 7px 14px;
    overflow: hidden;
    border-radius: 8px;
    color: var(--amber, #d6a542);
    font-size: clamp(17px, 1.8vw, 25px);
    font-weight: 800;
    line-height: 1.35;
}

.plan-deployment-notice::before {
    position: absolute;
    inset: 0;
    background:
        repeating-linear-gradient(
                90deg,
                currentcolor 0 8px,
                transparent 8px 12px,
                currentcolor 12px 14px,
                transparent 14px 18px
            )
            top left / 18px 2px repeat-x,
        repeating-linear-gradient(
                90deg,
                currentcolor 0 8px,
                transparent 8px 12px,
                currentcolor 12px 14px,
                transparent 14px 18px
            )
            bottom left / 18px 2px repeat-x,
        repeating-linear-gradient(
                180deg,
                currentcolor 0 8px,
                transparent 8px 12px,
                currentcolor 12px 14px,
                transparent 14px 18px
            )
            top left / 2px 18px repeat-y,
        repeating-linear-gradient(
                180deg,
                currentcolor 0 8px,
                transparent 8px 12px,
                currentcolor 12px 14px,
                transparent 14px 18px
            )
            top right / 2px 18px repeat-y;
    content: "";
    pointer-events: none;
}

.plans-section .page-shell {
    width: min(1400px, calc(100% - 48px));
}

.plans-section {
    padding-top: 52px;
}

.plans-grid {
    display: grid;
    grid-template-columns: repeat(5, minmax(0, 1fr));
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

.plan-card.enterprise,
.plan-card.plus {
    border-color: color-mix(in srgb, var(--primary) 50%, var(--border));
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

.plan-edition-slot {
    display: flex;
    min-height: 36px;
    align-items: center;
    margin-top: 18px;
}

.personal-edition-toggle {
    display: grid;
    width: 100%;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 3px;
    padding: 3px;
    border: 1px solid var(--border);
    border-radius: 9px;
    background: var(--secondary);
}

.personal-edition-toggle button {
    min-width: 0;
    min-height: 28px;
    padding: 0 6px;
    border: 0;
    border-radius: 6px;
    background: transparent;
    color: var(--muted-foreground);
    cursor: pointer;
    font: 700 11px var(--font-ui);
}

.personal-edition-toggle button.active {
    background: var(--card);
    box-shadow: 0 3px 10px rgba(15, 23, 42, 0.08);
    color: var(--primary);
}

.plan-price {
    display: grid;
    min-height: 112px;
    grid-template-rows: 16px 34px 18px;
    align-content: center;
    gap: 8px;
    margin: 18px -34px 0;
    padding: 0 34px;
    border-block: 1px solid var(--border);
    background: var(--secondary);
}

.plan-price small {
    display: block;
    height: 16px;
    color: var(--primary);
    font: 700 11px / 16px var(--font-ui);
    letter-spacing: 0;
}

.plan-price strong {
    display: block;
    height: 34px;
    font-size: 23px;
    font-weight: 760;
    line-height: 34px;
}

.plan-price span {
    display: block;
    height: 18px;
    color: var(--muted-foreground);
    font-size: 12px;
    line-height: 18px;
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

.featured > button,
.enterprise > button,
.plus > button {
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

.cloud-pricing-section {
    padding-top: 52px;
    background: var(--background);
}

.cloud-heading {
    max-width: 760px;
    margin: 0 auto 46px;
    text-align: center;
}

.cloud-products-grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 20px;
}

.cloud-products-grid article {
    display: grid;
    min-height: 370px;
    grid-template-columns: 56px 1fr;
    grid-template-rows: 1fr auto;
    gap: 24px;
    padding: 30px;
    border: 1px solid var(--border);
    border-radius: 22px;
    background: color-mix(in srgb, var(--card) 96%, transparent);
    box-shadow: 0 22px 54px rgba(15, 23, 42, 0.055);
}

.cloud-product-icon {
    display: grid;
    width: 56px;
    height: 56px;
    place-items: center;
    border-radius: 15px;
    background: var(--accent);
    color: var(--primary);
}

.cloud-product-copy > span {
    color: var(--primary);
    font: 700 10px var(--font-tech);
    letter-spacing: 0.13em;
}

.cloud-product-copy h3 {
    margin: 14px 0 12px;
    font-size: 27px;
    font-weight: 760;
}

.cloud-product-copy p {
    margin: 0;
    color: var(--muted-foreground);
    font-size: 13px;
    line-height: 1.75;
}

.cloud-product-copy ul {
    display: grid;
    gap: 11px;
    margin: 24px 0 0;
    padding: 0;
    list-style: none;
}

.cloud-product-copy li {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 12px;
}

.cloud-product-copy li svg {
    color: var(--primary);
}

.cloud-products-grid article > button {
    display: inline-flex;
    min-height: 44px;
    grid-column: 1 / -1;
    align-items: center;
    justify-content: center;
    gap: 8px;
    border: 1px solid var(--primary);
    border-radius: 10px;
    background: var(--primary);
    color: var(--primary-foreground);
    cursor: pointer;
    font: 700 13px var(--font-ui);
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
        grid-template-columns: repeat(2, 1fr);
    }

    .plan-card.plus {
        grid-column: 1 / -1;
    }

    .plan-heading p {
        min-height: 0;
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

    .cloud-products-grid {
        grid-template-columns: 1fr;
    }

    .faq-layout {
        grid-template-columns: 1fr;
        gap: 42px;
    }

    .faq-heading {
        position: static;
    }

}

@media (max-width: 560px) {
    .pricing-page-switcher {
        grid-template-columns: 1fr;
    }

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

    .plans-grid {
        grid-template-columns: 1fr;
    }

    .plan-card.plus {
        grid-column: auto;
    }

    .cloud-products-grid article {
        min-height: 0;
        grid-template-columns: 1fr;
        padding: 28px 24px;
    }

    .cloud-products-grid article > button {
        grid-column: auto;
    }

    .faq-list summary {
        grid-template-columns: 28px 1fr 20px;
        padding-inline: 20px;
    }

    .faq-list details > p {
        margin-right: 20px;
        margin-left: 48px;
    }

}
</style>
