<script setup lang="ts">
import { computed, onMounted, reactive, ref, type Component } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRoute, useRouter } from 'vue-router'
import {
    IconArrowLeft,
    IconArrowRight,
    IconBuildingSkyscraper,
    IconCheck,
    IconCopy,
    IconDeviceGamepad2,
    IconFileInvoice,
    IconLayersLinked,
    IconLink,
    IconMinus,
    IconPalette,
    IconPlus,
    IconPrinter,
    IconRocket,
    IconServerCog,
    IconSparkles,
    IconStack2,
    IconWorld,
} from '@tabler/icons-vue'

import ContactUs from '@/components/ContactUs.vue'
import { calculatePricing, formatMoney } from '@/pricing/calculate'
import { priceCatalog } from '@/pricing/catalog'
import type {
    BrandingKey,
    DeliveryKey,
    LicenseTerm,
    PricingCurrency,
    PricingSelection,
    PricingUsage,
    ProductKey,
} from '@/pricing/types'

interface ChoiceCard {
    key: string
    icon: Component
}

const props = withDefaults(
    defineProps<{
        embedded?: boolean
    }>(),
    {
        embedded: false,
    },
)

const { locale, t, tm } = useI18n()
const route = useRoute()
const router = useRouter()

const productKeys: ProductKey[] = ['gaming', 'rendering']
const productIcons: Record<ProductKey, Component> = {
    gaming: IconDeviceGamepad2,
    rendering: IconLayersLinked,
}
const stepIcons: Component[] = [
    IconBuildingSkyscraper,
    IconFileInvoice,
    IconStack2,
    IconServerCog,
    IconPalette,
    IconSparkles,
]
const usageChoices: ChoiceCard[] = [
    { key: 'internal', icon: IconBuildingSkyscraper },
    { key: 'oem', icon: IconWorld },
]
const licenseChoices: ChoiceCard[] = [
    { key: 'annual', icon: IconFileInvoice },
    { key: 'perpetual', icon: IconStack2 },
]
const deliveryChoices: ChoiceCard[] = [
    { key: 'self', icon: IconStack2 },
    { key: 'remote', icon: IconServerCog },
    { key: 'custom', icon: IconRocket },
]
const internalBrandingChoices: ChoiceCard[] = [
    { key: 'pixels', icon: IconSparkles },
    { key: 'basic', icon: IconPalette },
    { key: 'full', icon: IconLayersLinked },
    { key: 'custom', icon: IconServerCog },
]
const oemBrandingChoices: ChoiceCard[] = [
    { key: 'pixels', icon: IconPalette },
    { key: 'custom', icon: IconServerCog },
]

const currentStep = ref(0)
const calculatorRoot = ref<HTMLElement>()
const contactVisible = ref(false)
const feedbackMessage = ref('')
const validationMessage = ref('')
let feedbackTimer: ReturnType<typeof setTimeout> | undefined

const selection = reactive<PricingSelection>({
    usage: 'internal',
    licenseTerm: 'annual',
    currency: locale.value === 'en' ? 'USD' : 'CNY',
    quantities: {
        gaming: 5,
        rendering: 0,
    },
    delivery: 'self',
    branding: 'pixels',
})

const stepLabels = computed(() => tm('pricingCalculator.steps') as string[])
const estimate = computed(() => calculatePricing(selection))
const currencyCatalog = computed(
    () => priceCatalog.currencies[selection.currency],
)
const hasProducts = computed(() =>
    productKeys.some((product) => selection.quantities[product] > 0),
)
const selectedProductCount = computed(
    () => estimate.value.productLines.length,
)
const brandingChoices = computed(() =>
    selection.usage === 'oem'
        ? oemBrandingChoices
        : internalBrandingChoices,
)
const primaryAmountLabel = computed(() =>
    selection.licenseTerm === 'perpetual' && selection.usage === 'internal'
        ? t('pricingCalculator.firstPurchase')
        : t('pricingCalculator.firstYear'),
)
const usageLabel = computed(() =>
    t(`pricingCalculator.${selection.usage}.title`),
)
const termLabel = computed(() =>
    selection.usage === 'oem'
        ? t('pricingCalculator.annual.title')
        : t(`pricingCalculator.${selection.licenseTerm}.title`),
)
const deliveryLabel = computed(() =>
    t(`pricingCalculator.delivery.${selection.delivery}.title`),
)
const brandingLocaleKey = computed(() => {
    if (selection.usage === 'oem' && selection.branding === 'pixels') {
        return 'oemIncluded'
    }
    return selection.branding
})
const brandingLabel = computed(() =>
    t(`pricingCalculator.branding.${brandingLocaleKey.value}.title`),
)
const contactTitle = computed(() => t('pricingCalculator.contactTitle'))
const contactContent = computed(() => buildEstimateText())

function selectUsage(usage: PricingUsage) {
    selection.usage = usage
    if (usage === 'oem') {
        selection.licenseTerm = 'annual'
        selection.branding = 'pixels'
    }
}

function selectLicenseTerm(licenseTerm: LicenseTerm) {
    if (selection.usage === 'oem') return
    selection.licenseTerm = licenseTerm
}

function toggleProduct(product: ProductKey) {
    selection.quantities[product] =
        selection.quantities[product] > 0
            ? 0
            : currencyCatalog.value.minimumStreams
    validationMessage.value = ''
}

function updateQuantity(product: ProductKey, nextQuantity: number) {
    if (selection.quantities[product] === 0) return
    selection.quantities[product] = Math.max(
        currencyCatalog.value.minimumStreams,
        Math.floor(nextQuantity || currencyCatalog.value.minimumStreams),
    )
}

function adjustQuantity(product: ProductKey, increment: number) {
    updateQuantity(product, selection.quantities[product] + increment)
}

function selectDelivery(delivery: DeliveryKey) {
    selection.delivery = delivery
}

function selectBranding(branding: BrandingKey) {
    selection.branding = branding
}

function canOpenStep(stepIndex: number): boolean {
    return stepIndex < 5 || hasProducts.value
}

function openStep(stepIndex: number) {
    if (!canOpenStep(stepIndex)) {
        validationMessage.value = t('pricingCalculator.selectProduct')
        currentStep.value = 2
        return
    }
    currentStep.value = stepIndex
    validationMessage.value = ''
    if (props.embedded) {
        calculatorRoot.value
            ?.closest('.el-dialog__body')
            ?.scrollTo({ top: 0, behavior: 'smooth' })
    }
    else {
        window.scrollTo({ top: 0, behavior: 'smooth' })
    }
}

function nextStep() {
    if (currentStep.value === 2 && !hasProducts.value) {
        validationMessage.value = t('pricingCalculator.selectProduct')
        return
    }
    openStep(Math.min(currentStep.value + 1, 5))
}

function previousStep() {
    openStep(Math.max(currentStep.value - 1, 0))
}

function money(amount: number): string {
    return formatMoney(amount, selection.currency)
}

function productUnitPrice(product: ProductKey): number {
    const term = selection.usage === 'oem' ? 'annual' : selection.licenseTerm
    return currencyCatalog.value.products[product][term]
}

function deliveryPrice(delivery: string): string {
    if (delivery === 'self') return t('pricingCalculator.included')
    if (delivery === 'remote') {
        return t('pricingCalculator.startingAt', {
            amount: money(currencyCatalog.value.delivery.remote),
        })
    }
    return t('pricingCalculator.pendingAssessment')
}

function brandingPrice(branding: string): string {
    if (selection.usage === 'oem' && branding === 'pixels') {
        return t('pricingCalculator.startingAt', {
            amount: money(currencyCatalog.value.oem.setup),
        })
    }
    if (branding === 'pixels') return t('pricingCalculator.included')
    const amount =
        currencyCatalog.value.branding[
            branding as Exclude<BrandingKey, 'pixels'>
        ]
    return t('pricingCalculator.startingAt', { amount: money(amount) })
}

function showFeedback(message: string) {
    feedbackMessage.value = message
    if (feedbackTimer) clearTimeout(feedbackTimer)
    feedbackTimer = setTimeout(() => {
        feedbackMessage.value = ''
    }, 2400)
}

async function copyValue(value: string, successMessage: string) {
    try {
        await navigator.clipboard.writeText(value)
        showFeedback(successMessage)
    }
    catch {
        showFeedback(t('pricingCalculator.copyFailed'))
    }
}

function buildEstimateText(): string {
    const lines = [
        t('pricingCalculator.estimateHeading'),
        `${t('pricingCalculator.summaryUsage')}: ${usageLabel.value}`,
        `${t('pricingCalculator.summaryTerm')}: ${termLabel.value}`,
        `${t('pricingCalculator.summaryDeployment')}: ${t('pricingCalculator.oneDeployment')}`,
        '',
    ]

    for (const productLine of estimate.value.productLines) {
        lines.push(
            `${t(`pricingCalculator.products.${productLine.product}.title`)}: ${productLine.quantity} Streams`,
        )
    }

    lines.push(
        '',
        `${primaryAmountLabel.value}: ${money(estimate.value.firstYearAmount)}`,
        `${t('pricingCalculator.secondYear')}: ${money(estimate.value.secondYearAmount)}`,
        `${t('pricingCalculator.deliverySubtotal')}: ${deliveryLabel.value}`,
        `${t('pricingCalculator.brandingSubtotal')}: ${brandingLabel.value}`,
        '',
        t('pricingCalculator.singleDeployment'),
        t('pricingCalculator.estimateNotice'),
        `${t('pricingCalculator.catalogVersion')}: ${estimate.value.catalogVersion}`,
    )
    return lines.join('\n')
}

function queryValue(value: unknown): string | undefined {
    return typeof value === 'string' ? value : undefined
}

function restoreSharedSelection() {
    const usage = queryValue(route.query.usage)
    const term = queryValue(route.query.term)
    const currency = queryValue(route.query.currency)
    const delivery = queryValue(route.query.delivery)
    const branding = queryValue(route.query.branding)

    if (usage === 'internal' || usage === 'oem') selectUsage(usage)
    if (term === 'annual' || term === 'perpetual') selectLicenseTerm(term)
    if (currency === 'CNY' || currency === 'USD') selection.currency = currency
    if (delivery === 'self' || delivery === 'remote' || delivery === 'custom') {
        selection.delivery = delivery
    }
    if (
        branding === 'pixels' ||
        branding === 'basic' ||
        branding === 'full' ||
        branding === 'custom'
    ) {
        selection.branding = branding
    }
    if (
        selection.usage === 'oem' &&
        selection.branding !== 'pixels' &&
        selection.branding !== 'custom'
    ) {
        selection.branding = 'pixels'
    }

    for (const product of productKeys) {
        const parsedQuantity = Number(queryValue(route.query[product]))
        if (Number.isFinite(parsedQuantity) && parsedQuantity >= 0) {
            selection.quantities[product] =
                parsedQuantity === 0
                    ? 0
                    : Math.max(
                          currencyCatalog.value.minimumStreams,
                          Math.floor(parsedQuantity),
                      )
        }
    }

    if (queryValue(route.query.result) === '1' && hasProducts.value) {
        currentStep.value = 5
    }
}

function buildShareUrl(): string {
    const shareUrl = new URL(window.location.href)
    if (props.embedded) {
        shareUrl.pathname = router.resolve('/pricing/calculator').path
    }
    shareUrl.search = ''
    shareUrl.searchParams.set('usage', selection.usage)
    shareUrl.searchParams.set('term', selection.licenseTerm)
    shareUrl.searchParams.set('currency', selection.currency)
    shareUrl.searchParams.set('delivery', selection.delivery)
    shareUrl.searchParams.set('branding', selection.branding)
    for (const product of productKeys) {
        shareUrl.searchParams.set(
            product,
            String(selection.quantities[product]),
        )
    }
    shareUrl.searchParams.set('result', '1')
    return shareUrl.toString()
}

function printEstimate() {
    window.print()
}

onMounted(() => {
    if (!props.embedded) restoreSharedSelection()
})
</script>

<template>
  <ContactUs
    v-model="contactVisible"
    :initial-title="contactTitle"
    :initial-content="contactContent"
    initial-consult-type="enterprise"
  />

  <div
    ref="calculatorRoot"
    class="calculator-page"
    :class="{ embedded }"
  >
    <div class="calculator-grid-background" aria-hidden="true" />

    <header class="calculator-hero">
      <div class="calculator-shell hero-inner">
        <div class="hero-copy">
          <span class="eyebrow">{{ t('pricingCalculator.eyebrow') }}</span>
          <h1>{{ t('pricingCalculator.title') }}</h1>
          <p>{{ t('pricingCalculator.description') }}</p>
          <div class="deployment-note">
            <IconStack2 :size="18" :stroke-width="1.7" />
            {{ t('pricingCalculator.singleDeployment') }}
          </div>
        </div>

        <div class="catalog-control" aria-label="Pricing catalog">
          <span>{{ t('pricingCalculator.currency') }}</span>
          <div class="currency-switch">
            <button
              v-for="currencyOption in (['CNY', 'USD'] as PricingCurrency[])"
              :key="currencyOption"
              type="button"
              :class="{ active: selection.currency === currencyOption }"
              @click="selection.currency = currencyOption"
            >
              {{ currencyOption === 'CNY' ? 'CNY ¥' : 'USD $' }}
            </button>
          </div>
          <small>
            {{ t('pricingCalculator.catalogVersion') }}
            {{ priceCatalog.version }} · {{ priceCatalog.effectiveFrom }}
          </small>
        </div>
      </div>
    </header>

    <div class="calculator-shell calculator-layout">
      <section class="calculator-workspace">
        <nav class="stepper" :aria-label="t('pricingCalculator.progress')">
          <button
            v-for="(stepLabel, stepIndex) in stepLabels"
            :key="stepLabel"
            type="button"
            :class="{
              active: currentStep === stepIndex,
              complete: currentStep > stepIndex,
            }"
            :disabled="!canOpenStep(stepIndex)"
            @click="openStep(stepIndex)"
          >
            <span>
              <IconCheck v-if="currentStep > stepIndex" :size="14" :stroke-width="2.2" />
              <component v-else :is="stepIcons[stepIndex]" :size="16" :stroke-width="1.7" />
            </span>
            <small>0{{ stepIndex + 1 }}</small>
            <strong>{{ stepLabel }}</strong>
          </button>
        </nav>

        <div class="step-mobile-progress">
          <span>{{ t('pricingCalculator.stepOf', { current: currentStep + 1, total: 6 }) }}</span>
          <strong>{{ stepLabels[currentStep] }}</strong>
          <i><b :style="{ width: `${((currentStep + 1) / 6) * 100}%` }" /></i>
        </div>

        <div class="step-panel">
          <Transition name="step-fade" mode="out-in">
            <div :key="currentStep" class="step-content">
              <template v-if="currentStep === 0">
                <div class="step-heading">
                  <span>01 / USAGE</span>
                  <h2>{{ t('pricingCalculator.usageTitle') }}</h2>
                  <p>{{ t('pricingCalculator.usageDescription') }}</p>
                </div>
                <div class="choice-grid two-columns">
                  <button
                    v-for="usageChoice in usageChoices"
                    :key="usageChoice.key"
                    type="button"
                    class="choice-card tall-card"
                    :class="{ selected: selection.usage === usageChoice.key }"
                    @click="selectUsage(usageChoice.key as PricingUsage)"
                  >
                    <span class="selection-check">
                      <IconCheck :size="13" :stroke-width="2.3" />
                    </span>
                    <span class="choice-icon">
                      <component :is="usageChoice.icon" :size="26" :stroke-width="1.5" />
                    </span>
                    <strong>{{ t(`pricingCalculator.${usageChoice.key}.title`) }}</strong>
                    <p>{{ t(`pricingCalculator.${usageChoice.key}.description`) }}</p>
                    <ul>
                      <li
                        v-for="point in (tm(`pricingCalculator.${usageChoice.key}.points`) as string[])"
                        :key="point"
                      >
                        <IconCheck :size="13" :stroke-width="2" />{{ point }}
                      </li>
                    </ul>
                  </button>
                </div>
              </template>

              <template v-else-if="currentStep === 1">
                <div class="step-heading">
                  <span>02 / LICENSE</span>
                  <h2>{{ t('pricingCalculator.licenseTitle') }}</h2>
                  <p>{{ t('pricingCalculator.licenseDescription') }}</p>
                </div>
                <div v-if="selection.usage === 'oem'" class="oem-license-banner">
                  <span><IconWorld :size="23" :stroke-width="1.6" /></span>
                  <div>
                    <strong>{{ t('pricingCalculator.annual.title') }}</strong>
                    <p>{{ t('pricingCalculator.oemAnnualOnly') }}</p>
                  </div>
                  <IconCheck :size="20" :stroke-width="2" />
                </div>
                <div v-else class="choice-grid two-columns">
                  <button
                    v-for="licenseChoice in licenseChoices"
                    :key="licenseChoice.key"
                    type="button"
                    class="choice-card license-card"
                    :class="{ selected: selection.licenseTerm === licenseChoice.key }"
                    @click="selectLicenseTerm(licenseChoice.key as LicenseTerm)"
                  >
                    <span class="selection-check">
                      <IconCheck :size="13" :stroke-width="2.3" />
                    </span>
                    <span class="choice-icon">
                      <component :is="licenseChoice.icon" :size="26" :stroke-width="1.5" />
                    </span>
                    <div class="choice-title-row">
                      <strong>{{ t(`pricingCalculator.${licenseChoice.key}.title`) }}</strong>
                      <em v-if="licenseChoice.key === 'annual'">
                        {{ t('pricingCalculator.annual.badge') }}
                      </em>
                    </div>
                    <p>{{ t(`pricingCalculator.${licenseChoice.key}.description`) }}</p>
                    <small>{{ t(`pricingCalculator.${licenseChoice.key}.note`) }}</small>
                  </button>
                </div>
              </template>

              <template v-else-if="currentStep === 2">
                <div class="step-heading">
                  <span>03 / PRODUCTS</span>
                  <h2>{{ t('pricingCalculator.productsTitle') }}</h2>
                  <p>{{ t('pricingCalculator.productsDescription') }}</p>
                </div>
                <div class="example-note">
                  <IconSparkles :size="17" :stroke-width="1.7" />
                  {{ t('pricingCalculator.streamExample') }}
                </div>
                <div class="product-selection-list">
                  <article
                    v-for="(product, productIndex) in productKeys"
                    :key="product"
                    class="product-selector"
                    :class="{ selected: selection.quantities[product] > 0 }"
                  >
                    <button
                      class="product-toggle"
                      type="button"
                      :aria-pressed="selection.quantities[product] > 0"
                      @click="toggleProduct(product)"
                    >
                      <span class="product-number">0{{ productIndex + 1 }}</span>
                      <span class="product-icon">
                        <component :is="productIcons[product]" :size="25" :stroke-width="1.55" />
                      </span>
                      <span class="product-copy">
                        <strong>{{ t(`pricingCalculator.products.${product}.title`) }}</strong>
                        <small>{{ t(`pricingCalculator.products.${product}.description`) }}</small>
                      </span>
                      <span class="product-price">
                        <b>{{ money(productUnitPrice(product)) }}</b>
                        <small>
                          {{ selection.licenseTerm === 'perpetual' && selection.usage === 'internal'
                            ? t('pricingCalculator.perStreamPerpetual')
                            : t('pricingCalculator.perStreamYear') }}
                        </small>
                      </span>
                      <span class="large-check"><IconCheck :size="16" :stroke-width="2.2" /></span>
                    </button>
                    <div v-if="selection.quantities[product] > 0" class="quantity-editor">
                      <span>{{ t('pricingCalculator.streams') }}</span>
                      <div>
                        <button
                          type="button"
                          :aria-label="t('pricingCalculator.decrease')"
                          @click="adjustQuantity(product, -1)"
                        >
                          <IconMinus :size="16" :stroke-width="2" />
                        </button>
                        <input
                          :value="selection.quantities[product]"
                          type="number"
                          inputmode="numeric"
                          :min="currencyCatalog.minimumStreams"
                          :aria-label="t('pricingCalculator.customQuantity')"
                          @input="updateQuantity(product, Number(($event.target as HTMLInputElement).value))"
                        >
                        <button
                          type="button"
                          :aria-label="t('pricingCalculator.increase')"
                          @click="adjustQuantity(product, 1)"
                        >
                          <IconPlus :size="16" :stroke-width="2" />
                        </button>
                      </div>
                      <small>
                        {{ t('pricingCalculator.minimum', { count: currencyCatalog.minimumStreams }) }}
                      </small>
                      <strong>{{ money(productUnitPrice(product) * selection.quantities[product]) }}</strong>
                    </div>
                  </article>
                </div>
                <p v-if="validationMessage" class="validation-message" role="alert">
                  {{ validationMessage }}
                </p>
              </template>

              <template v-else-if="currentStep === 3">
                <div class="step-heading">
                  <span>04 / DELIVERY</span>
                  <h2>{{ t('pricingCalculator.deliveryTitle') }}</h2>
                  <p>{{ t('pricingCalculator.deliveryDescription') }}</p>
                </div>
                <div class="choice-grid three-columns">
                  <button
                    v-for="deliveryChoice in deliveryChoices"
                    :key="deliveryChoice.key"
                    type="button"
                    class="choice-card compact-card"
                    :class="{ selected: selection.delivery === deliveryChoice.key }"
                    @click="selectDelivery(deliveryChoice.key as DeliveryKey)"
                  >
                    <span class="selection-check"><IconCheck :size="13" :stroke-width="2.3" /></span>
                    <span class="choice-icon">
                      <component :is="deliveryChoice.icon" :size="25" :stroke-width="1.5" />
                    </span>
                    <strong>{{ t(`pricingCalculator.delivery.${deliveryChoice.key}.title`) }}</strong>
                    <p>{{ t(`pricingCalculator.delivery.${deliveryChoice.key}.description`) }}</p>
                    <small>{{ deliveryPrice(deliveryChoice.key) }}</small>
                  </button>
                </div>
              </template>

              <template v-else-if="currentStep === 4">
                <div class="step-heading">
                  <span>05 / BRAND</span>
                  <h2>{{ t('pricingCalculator.brandingTitle') }}</h2>
                  <p>{{ t('pricingCalculator.brandingDescription') }}</p>
                </div>
                <div class="choice-grid two-columns branding-grid">
                  <button
                    v-for="brandingChoice in brandingChoices"
                    :key="brandingChoice.key"
                    type="button"
                    class="choice-card compact-card"
                    :class="{ selected: selection.branding === brandingChoice.key }"
                    @click="selectBranding(brandingChoice.key as BrandingKey)"
                  >
                    <span class="selection-check"><IconCheck :size="13" :stroke-width="2.3" /></span>
                    <span class="choice-icon">
                      <component :is="brandingChoice.icon" :size="25" :stroke-width="1.5" />
                    </span>
                    <strong>
                      {{ t(`pricingCalculator.branding.${selection.usage === 'oem' && brandingChoice.key === 'pixels' ? 'oemIncluded' : brandingChoice.key}.title`) }}
                    </strong>
                    <p>
                      {{ t(`pricingCalculator.branding.${selection.usage === 'oem' && brandingChoice.key === 'pixels' ? 'oemIncluded' : brandingChoice.key}.description`) }}
                    </p>
                    <small>{{ brandingPrice(brandingChoice.key) }}</small>
                  </button>
                </div>
              </template>

              <template v-else>
                <div class="step-heading result-heading">
                  <span>06 / ESTIMATE</span>
                  <h2>{{ t('pricingCalculator.resultTitle') }}</h2>
                  <p>{{ t('pricingCalculator.resultDescription') }}</p>
                </div>

                <div class="result-main-grid">
                  <div class="result-primary-column">
                    <div class="result-amounts">
                      <article class="primary-total">
                        <span>{{ primaryAmountLabel }}</span>
                        <strong>{{ money(estimate.firstYearAmount) }}</strong>
                        <small>{{ t('pricingCalculator.formalQuote') }} · {{ selection.currency }}</small>
                      </article>
                      <article>
                        <span>{{ t('pricingCalculator.secondYear') }}</span>
                        <strong>{{ money(estimate.secondYearAmount) }}</strong>
                        <small>{{ t('pricingCalculator.secondYearDescription') }}</small>
                      </article>
                    </div>

                    <div class="result-breakdown">
                      <div class="breakdown-heading">
                        <strong>{{ t('pricingCalculator.licenseSubtotal') }}</strong>
                        <span>{{ money(estimate.licenseAmount) }}</span>
                      </div>
                      <div
                        v-for="productLine in estimate.productLines"
                        :key="productLine.product"
                        class="breakdown-row"
                      >
                        <span>
                          {{ t(`pricingCalculator.products.${productLine.product}.title`) }}
                          <small>{{ productLine.quantity }} Streams × {{ money(productLine.unitPrice) }}</small>
                        </span>
                        <strong>{{ money(productLine.amount) }}</strong>
                      </div>
                      <div v-if="estimate.oemMinimumAdjustmentAmount" class="breakdown-row">
                        <span>
                          {{ t('pricingCalculator.oemMinimumAdjustment') }}
                          <small>{{ t('pricingCalculator.oem.annualMinimum') }}</small>
                        </span>
                        <strong>{{ money(estimate.oemMinimumAdjustmentAmount) }}</strong>
                      </div>
                      <div class="breakdown-row">
                        <span>{{ t('pricingCalculator.deliverySubtotal') }}<small>{{ deliveryLabel }}</small></span>
                        <strong v-if="!estimate.requiresDeliveryAssessment">{{ money(estimate.deliveryAmount) }}</strong>
                        <strong v-else>{{ t('pricingCalculator.pendingAssessment') }}</strong>
                      </div>
                      <div class="breakdown-row">
                        <span>{{ t('pricingCalculator.brandingSubtotal') }}<small>{{ brandingLabel }}</small></span>
                        <strong>{{ money(estimate.brandingAmount) }}</strong>
                      </div>
                      <div v-if="estimate.coreMaintenanceAmount" class="breakdown-row future-row">
                        <span>{{ t('pricingCalculator.coreMaintenance') }}<small>{{ t('pricingCalculator.secondYear') }}</small></span>
                        <strong>{{ money(estimate.coreMaintenanceAmount) }}</strong>
                      </div>
                      <div v-if="estimate.brandingMaintenanceAmount" class="breakdown-row future-row">
                        <span>{{ t('pricingCalculator.brandingMaintenance') }}<small>{{ t('pricingCalculator.secondYear') }}</small></span>
                        <strong>{{ money(estimate.brandingMaintenanceAmount) }}</strong>
                      </div>
                    </div>
                  </div>

                  <div class="result-secondary-column">
                    <div v-if="estimate.requiresDeliveryAssessment || estimate.requiresCustomizationAssessment" class="assessment-warning">
                      <IconSparkles :size="19" :stroke-width="1.7" />
                      {{ t('pricingCalculator.assessmentWarning') }}
                    </div>

                    <div class="sales-note">
                      <div><IconWorld :size="23" :stroke-width="1.55" /></div>
                      <span>
                        <strong>{{ t('pricingCalculator.noDiscountTitle') }}</strong>
                        <p>{{ t('pricingCalculator.noDiscountDescription') }}</p>
                      </span>
                    </div>

                    <div class="excluded-box">
                      <strong>{{ t('pricingCalculator.excludedTitle') }}</strong>
                      <ul>
                        <li v-for="excludedItem in (tm('pricingCalculator.excludedItems') as string[])" :key="excludedItem">
                          <IconMinus :size="13" :stroke-width="2" />{{ excludedItem }}
                        </li>
                      </ul>
                    </div>

                    <p class="estimate-notice">{{ t('pricingCalculator.estimateNotice') }}</p>

                    <div class="result-actions">
                      <button class="result-action primary" type="button" @click="contactVisible = true">
                        {{ t('pricingCalculator.contact') }}
                        <IconArrowRight :size="17" :stroke-width="1.9" />
                      </button>
                      <button class="result-action" type="button" @click="copyValue(buildEstimateText(), t('pricingCalculator.copied'))">
                        <IconCopy :size="17" :stroke-width="1.7" />{{ t('pricingCalculator.copy') }}
                      </button>
                      <button class="result-action" type="button" @click="copyValue(buildShareUrl(), t('pricingCalculator.linkCopied'))">
                        <IconLink :size="17" :stroke-width="1.7" />{{ t('pricingCalculator.copyLink') }}
                      </button>
                      <button class="result-action" type="button" @click="printEstimate">
                        <IconPrinter :size="17" :stroke-width="1.7" />{{ t('pricingCalculator.print') }}
                      </button>
                    </div>
                  </div>
                </div>
              </template>
            </div>
          </Transition>

          <div v-if="currentStep < 5" class="step-navigation">
            <button v-if="currentStep > 0" type="button" class="back-button" @click="previousStep">
              <IconArrowLeft :size="17" :stroke-width="1.8" />{{ t('pricingCalculator.previous') }}
            </button>
            <span v-else />
            <button type="button" class="next-button" @click="nextStep">
              {{ currentStep === 4 ? t('pricingCalculator.viewResult') : t('pricingCalculator.next') }}
              <IconArrowRight :size="17" :stroke-width="1.9" />
            </button>
          </div>
          <div v-else class="step-navigation edit-navigation">
            <button type="button" class="back-button" @click="openStep(2)">
              <IconArrowLeft :size="17" :stroke-width="1.8" />{{ t('pricingCalculator.edit') }}
            </button>
          </div>
        </div>
      </section>

      <aside class="estimate-sidebar">
        <div class="summary-card">
          <div class="summary-header">
            <span>{{ t('pricingCalculator.formalQuote') }}</span>
            <IconFileInvoice :size="22" :stroke-width="1.5" />
          </div>
          <div class="summary-total">
            <small>{{ primaryAmountLabel }}</small>
            <strong>{{ hasProducts ? money(estimate.firstYearAmount) : '—' }}</strong>
            <span>{{ selection.currency }} · {{ t('pricingCalculator.oneDeployment') }}</span>
          </div>
          <dl class="summary-meta">
            <div><dt>{{ t('pricingCalculator.summaryUsage') }}</dt><dd>{{ usageLabel }}</dd></div>
            <div><dt>{{ t('pricingCalculator.summaryTerm') }}</dt><dd>{{ termLabel }}</dd></div>
            <div><dt>{{ t('pricingCalculator.summaryDeployment') }}</dt><dd>{{ t('pricingCalculator.oneDeployment') }}</dd></div>
          </dl>
          <div class="summary-products">
            <span>{{ t('pricingCalculator.productsTitle') }}</span>
            <div v-if="selectedProductCount">
              <p v-for="productLine in estimate.productLines" :key="productLine.product">
                <strong>{{ t(`pricingCalculator.products.${productLine.product}.title`) }}</strong>
                <small>{{ productLine.quantity }} Streams</small>
              </p>
            </div>
            <p v-else class="empty-products">{{ t('pricingCalculator.selectProduct') }}</p>
          </div>
          <div class="summary-recurring">
            <span>{{ t('pricingCalculator.secondYear') }}</span>
            <strong>{{ hasProducts ? money(estimate.secondYearAmount) : '—' }}</strong>
          </div>
          <p class="summary-note">{{ t('pricingCalculator.singleDeployment') }}</p>
        </div>
      </aside>
    </div>

    <Transition name="feedback">
      <div v-if="feedbackMessage" class="feedback-toast" role="status">
        <IconCheck :size="17" :stroke-width="2" />{{ feedbackMessage }}
      </div>
    </Transition>
  </div>
</template>

<style scoped>
.calculator-page {
    --background: #f7f8f7;
    --foreground: #18181b;
    --card: #ffffff;
    --secondary: #f1f3f2;
    --muted-foreground: #71717a;
    --border: #e1e5e3;
    --primary: #007f49;
    --primary-bright: #009a59;
    --primary-soft: #e9f9f1;
    position: relative;
    min-height: calc(100vh - 72px);
    overflow: hidden;
    background: var(--background);
    color: var(--foreground);
}

.calculator-page.embedded {
    height: 100%;
    min-height: 0;
    overflow: hidden;
}

.embedded .calculator-hero {
    padding: 10px 0;
}

.embedded .calculator-shell {
    width: min(1320px, calc(100% - 32px));
}

.embedded .hero-inner {
    justify-content: flex-end;
}

.embedded .hero-copy {
    display: none;
}

.embedded .catalog-control {
    display: grid;
    width: 100%;
    min-width: 0;
    grid-template-columns: auto 180px auto;
    align-items: center;
    gap: 14px;
    padding: 7px 10px 7px 14px;
    border-radius: 11px;
    box-shadow: none;
}

.embedded .catalog-control small {
    justify-self: end;
}

.embedded .calculator-layout {
    display: grid;
    grid-template-columns: minmax(0, 1fr) 300px;
    gap: 18px;
    padding-bottom: 14px;
}

.embedded .stepper button {
    min-height: 62px;
    grid-template-columns: 24px 1fr;
    gap: 2px 7px;
    padding: 8px 10px;
}

.embedded .stepper button > span {
    width: 24px;
    height: 24px;
}

.embedded .step-panel {
    display: flex;
    height: min(625px, calc(100vh - 250px));
    min-height: 0;
    flex-direction: column;
    padding: 24px 28px;
}

.embedded .step-content {
    min-height: 0;
    flex: 1 1 auto;
}

.embedded .step-heading {
    margin-bottom: 16px;
}

.embedded .step-heading h2 {
    margin: 6px 0;
    font-size: clamp(24px, 2.2vw, 30px);
}

.embedded .step-heading p {
    font-size: 12px;
    line-height: 1.5;
}

.embedded .choice-card {
    padding: 17px;
}

.embedded .choice-icon {
    width: 42px;
    height: 42px;
    margin-bottom: 13px;
}

.embedded .choice-card > p {
    min-height: 36px;
    margin-top: 7px;
    font-size: 11px;
    line-height: 1.5;
}

.embedded .choice-card > ul {
    gap: 6px;
    margin-top: 12px;
    padding-top: 12px;
}

.embedded .choice-card > ul li {
    font-size: 10px;
}

.embedded .tall-card {
    min-height: 218px;
}

.embedded .license-card {
    min-height: 190px;
}

.embedded .license-card > small,
.embedded .compact-card > small {
    padding-top: 12px;
}

.embedded .example-note {
    margin: -7px 0 12px;
    font-size: 10px;
}

.embedded .product-selection-list {
    gap: 8px;
}

.embedded .product-toggle {
    min-height: 76px;
    padding: 10px 14px;
}

.embedded .quantity-editor {
    padding: 8px 14px;
}

.embedded .compact-card,
.embedded .branding-grid .compact-card {
    min-height: 176px;
}

.embedded .branding-grid {
    grid-template-columns: repeat(auto-fit, minmax(170px, 1fr));
}

.embedded .three-columns {
    grid-template-columns: repeat(3, minmax(0, 1fr));
}

.embedded .step-navigation {
    margin-top: 14px;
    padding-top: 12px;
}

.embedded .back-button,
.embedded .next-button {
    min-height: 36px;
}

.embedded .estimate-sidebar {
    position: static;
}

.embedded .summary-header {
    height: 46px;
}

.embedded .summary-total {
    gap: 5px;
    padding: 16px 18px;
}

.embedded .summary-total strong {
    font-size: 24px;
}

.embedded .summary-meta {
    padding: 8px 18px;
}

.embedded .summary-meta div {
    padding: 4px 0;
}

.embedded .summary-products {
    padding: 12px 18px;
}

.embedded .summary-products > div {
    gap: 6px;
    margin-top: 8px;
}

.embedded .summary-recurring {
    padding: 12px 18px;
}

.embedded .summary-note {
    padding: 10px 18px;
}

.embedded .result-heading {
    margin-bottom: 12px;
}

.embedded .result-main-grid {
    display: grid;
    grid-template-columns: minmax(0, 1.08fr) minmax(250px, 0.92fr);
    gap: 12px;
}

.embedded .result-primary-column,
.embedded .result-secondary-column {
    display: flex;
    min-width: 0;
    flex-direction: column;
    gap: 10px;
}

.embedded .result-amounts {
    gap: 8px;
}

.embedded .result-amounts article {
    min-height: 88px;
    gap: 4px;
    padding: 12px;
}

.embedded .result-amounts strong {
    font-size: 23px;
}

.embedded .result-breakdown {
    margin-top: 0;
}

.embedded .breakdown-heading,
.embedded .breakdown-row {
    gap: 12px;
    padding: 8px 12px;
}

.embedded .breakdown-row > span {
    gap: 2px;
    font-size: 11px;
}

.embedded .assessment-warning,
.embedded .sales-note,
.embedded .excluded-box,
.embedded .estimate-notice,
.embedded .result-actions {
    margin-top: 0;
}

.embedded .assessment-warning,
.embedded .sales-note {
    padding: 11px;
}

.embedded .sales-note > div {
    width: 34px;
    height: 34px;
}

.embedded .sales-note p {
    margin-top: 2px;
    font-size: 10px;
    line-height: 1.4;
}

.embedded .excluded-box {
    padding: 11px 13px;
}

.embedded .excluded-box ul {
    gap: 5px 10px;
    margin-top: 7px;
}

.embedded .estimate-notice {
    font-size: 9px;
    line-height: 1.45;
}

.embedded .result-actions {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 7px;
}

.embedded .result-action {
    min-height: 34px;
    padding-inline: 9px;
    font-size: 10px;
}

.calculator-grid-background {
    position: absolute;
    inset: 0 0 auto;
    height: 520px;
    background-image:
        linear-gradient(color-mix(in srgb, var(--border) 62%, transparent) 1px, transparent 1px),
        linear-gradient(90deg, color-mix(in srgb, var(--border) 62%, transparent) 1px, transparent 1px);
    background-size: 48px 48px;
    mask-image: linear-gradient(to bottom, black, transparent 88%);
    pointer-events: none;
}

.calculator-grid-background::after {
    position: absolute;
    inset: 0;
    background: radial-gradient(circle at 32% 22%, rgba(0, 154, 89, 0.12), transparent 34%);
    content: '';
}

.calculator-shell {
    position: relative;
    width: min(1280px, calc(100% - 48px));
    margin: 0 auto;
}

.calculator-hero {
    position: relative;
    padding: 74px 0 42px;
}

.hero-inner {
    display: flex;
    align-items: flex-end;
    justify-content: space-between;
    gap: 56px;
}

.hero-copy {
    max-width: 760px;
}

.eyebrow,
.step-heading > span {
    color: var(--primary);
    font: 700 11px var(--font-tech);
    letter-spacing: 0.12em;
}

.hero-copy h1 {
    max-width: 750px;
    margin: 20px 0 18px;
    font: 760 clamp(44px, 5vw, 72px) / 1.05 var(--font-ui);
    letter-spacing: -0.06em;
}

.hero-copy > p {
    max-width: 690px;
    margin: 0;
    color: var(--muted-foreground);
    font-size: 17px;
    line-height: 1.8;
}

.deployment-note {
    display: inline-flex;
    align-items: center;
    gap: 9px;
    margin-top: 23px;
    padding: 9px 13px;
    border: 1px solid color-mix(in srgb, var(--primary) 22%, var(--border));
    border-radius: 10px;
    background: color-mix(in srgb, var(--primary-soft) 70%, transparent);
    color: var(--primary);
    font-size: 12px;
    font-weight: 650;
}

.catalog-control {
    display: grid;
    min-width: 246px;
    gap: 10px;
    padding: 18px;
    border: 1px solid var(--border);
    border-radius: 16px;
    background: color-mix(in srgb, var(--card) 88%, transparent);
    box-shadow: 0 18px 48px rgba(24, 24, 27, 0.06);
    backdrop-filter: blur(14px);
}

.catalog-control > span {
    color: var(--muted-foreground);
    font-size: 11px;
    font-weight: 700;
}

.catalog-control small {
    color: var(--muted-foreground);
    font: 9px var(--font-tech);
}

.currency-switch {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 5px;
    padding: 4px;
    border-radius: 10px;
    background: var(--secondary);
}

.currency-switch button {
    height: 34px;
    border: 0;
    border-radius: 7px;
    background: transparent;
    color: var(--muted-foreground);
    cursor: pointer;
    font: 700 11px var(--font-tech);
}

.currency-switch button.active {
    background: var(--card);
    box-shadow: 0 3px 10px rgba(24, 24, 27, 0.08);
    color: var(--primary);
}

.calculator-layout {
    display: grid;
    grid-template-columns: minmax(0, 1fr) 330px;
    align-items: start;
    gap: 28px;
    padding-bottom: 100px;
}

.calculator-workspace {
    min-width: 0;
}

.stepper {
    display: grid;
    grid-template-columns: repeat(6, 1fr);
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 16px 16px 0 0;
    background: var(--card);
}

.stepper button {
    position: relative;
    display: grid;
    min-width: 0;
    min-height: 86px;
    grid-template-columns: 28px 1fr;
    grid-template-rows: auto auto;
    align-content: center;
    gap: 4px 8px;
    padding: 13px 12px;
    border: 0;
    border-right: 1px solid var(--border);
    background: transparent;
    color: var(--muted-foreground);
    cursor: pointer;
    text-align: left;
}

.stepper button:last-child {
    border-right: 0;
}

.stepper button:disabled {
    cursor: not-allowed;
    opacity: 0.46;
}

.stepper button > span {
    display: grid;
    width: 28px;
    height: 28px;
    grid-row: 1 / 3;
    place-items: center;
    border-radius: 9px;
    background: var(--secondary);
}

.stepper button small {
    font: 9px var(--font-tech);
}

.stepper button strong {
    overflow: hidden;
    font-size: 11px;
    font-weight: 700;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.stepper button.active,
.stepper button.complete {
    background: color-mix(in srgb, var(--primary-soft) 44%, var(--card));
    color: var(--primary);
}

.stepper button.active::after {
    position: absolute;
    right: 12px;
    bottom: 0;
    left: 12px;
    height: 3px;
    border-radius: 3px 3px 0 0;
    background: var(--primary);
    content: '';
}

.stepper button.active > span,
.stepper button.complete > span {
    background: var(--primary);
    color: #ffffff;
}

.step-mobile-progress {
    display: none;
}

.step-panel {
    min-height: 650px;
    padding: 48px;
    border: 1px solid var(--border);
    border-top: 0;
    border-radius: 0 0 18px 18px;
    background: var(--card);
    box-shadow: 0 24px 70px rgba(24, 24, 27, 0.06);
}

.step-content {
    min-height: 500px;
}

.step-heading {
    max-width: 690px;
    margin-bottom: 34px;
}

.step-heading h2 {
    margin: 12px 0 12px;
    font: 740 clamp(29px, 3vw, 42px) / 1.15 var(--font-ui);
    letter-spacing: -0.045em;
}

.step-heading p {
    margin: 0;
    color: var(--muted-foreground);
    font-size: 14px;
    line-height: 1.75;
}

.choice-grid {
    display: grid;
    gap: 14px;
}

.two-columns {
    grid-template-columns: repeat(2, minmax(0, 1fr));
}

.three-columns {
    grid-template-columns: repeat(3, minmax(0, 1fr));
}

.choice-card {
    position: relative;
    display: flex;
    min-width: 0;
    flex-direction: column;
    align-items: flex-start;
    padding: 24px;
    border: 1px solid var(--border);
    border-radius: 15px;
    background: var(--card);
    color: var(--foreground);
    cursor: pointer;
    text-align: left;
    transition: border-color 160ms ease, box-shadow 160ms ease, transform 160ms ease;
}

.choice-card:hover {
    border-color: color-mix(in srgb, var(--primary) 38%, var(--border));
    transform: translateY(-2px);
}

.choice-card.selected {
    border-color: var(--primary);
    background: linear-gradient(145deg, color-mix(in srgb, var(--primary-soft) 62%, var(--card)), var(--card) 64%);
    box-shadow: 0 16px 36px rgba(0, 127, 73, 0.09);
}

.selection-check,
.large-check {
    position: absolute;
    display: grid;
    place-items: center;
    border: 1px solid var(--border);
    color: transparent;
}

.selection-check {
    top: 18px;
    right: 18px;
    width: 22px;
    height: 22px;
    border-radius: 50%;
}

.choice-card.selected .selection-check,
.product-selector.selected .large-check {
    border-color: var(--primary);
    background: var(--primary);
    color: #ffffff;
}

.choice-icon,
.product-icon {
    display: grid;
    place-items: center;
    border-radius: 12px;
    background: var(--primary-soft);
    color: var(--primary);
}

.choice-icon {
    width: 50px;
    height: 50px;
    margin-bottom: 28px;
}

.choice-card > strong,
.choice-title-row strong {
    font-size: 17px;
    font-weight: 750;
}

.choice-card > p {
    min-height: 48px;
    margin: 10px 0 0;
    color: var(--muted-foreground);
    font-size: 13px;
    line-height: 1.65;
}

.choice-card > ul {
    display: grid;
    gap: 9px;
    margin: 25px 0 0;
    padding: 20px 0 0;
    border-top: 1px solid var(--border);
    list-style: none;
}

.choice-card > ul li {
    display: flex;
    align-items: center;
    gap: 8px;
    color: var(--muted-foreground);
    font-size: 12px;
}

.choice-card > ul svg {
    color: var(--primary);
}

.tall-card {
    min-height: 320px;
}

.license-card {
    min-height: 270px;
}

.choice-title-row {
    display: flex;
    align-items: center;
    gap: 10px;
}

.choice-title-row em {
    padding: 5px 8px;
    border-radius: 7px;
    background: var(--primary);
    color: #fff;
    font-size: 9px;
    font-style: normal;
    font-weight: 700;
}

.license-card > small,
.compact-card > small {
    margin-top: auto;
    padding-top: 26px;
    color: var(--primary);
    font-size: 11px;
    font-weight: 700;
}

.oem-license-banner {
    display: grid;
    grid-template-columns: 54px 1fr 30px;
    align-items: center;
    gap: 18px;
    padding: 27px;
    border: 1px solid var(--primary);
    border-radius: 16px;
    background: linear-gradient(120deg, var(--primary-soft), var(--card));
    color: var(--primary);
}

.oem-license-banner > span {
    display: grid;
    width: 54px;
    height: 54px;
    place-items: center;
    border-radius: 14px;
    background: var(--primary);
    color: #fff;
}

.oem-license-banner strong {
    color: var(--foreground);
    font-size: 18px;
}

.oem-license-banner p {
    margin: 7px 0 0;
    color: var(--muted-foreground);
    font-size: 13px;
    line-height: 1.6;
}

.example-note {
    display: flex;
    align-items: center;
    gap: 9px;
    margin: -12px 0 22px;
    color: var(--primary);
    font-size: 12px;
    font-weight: 650;
}

.product-selection-list {
    display: grid;
    gap: 12px;
}

.product-selector {
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 15px;
    background: var(--card);
    transition: border-color 160ms ease, box-shadow 160ms ease;
}

.product-selector.selected {
    border-color: var(--primary);
    box-shadow: 0 12px 30px rgba(0, 127, 73, 0.08);
}

.product-toggle {
    position: relative;
    display: grid;
    width: 100%;
    min-height: 104px;
    grid-template-columns: 30px 48px minmax(170px, 1fr) auto 26px;
    align-items: center;
    gap: 15px;
    padding: 18px 20px;
    border: 0;
    background: transparent;
    color: var(--foreground);
    cursor: pointer;
    text-align: left;
}

.product-number {
    align-self: start;
    color: var(--muted-foreground);
    font: 10px var(--font-tech);
}

.product-icon {
    width: 48px;
    height: 48px;
}

.product-copy {
    display: grid;
    gap: 7px;
}

.product-copy strong {
    font-size: 16px;
    font-weight: 750;
}

.product-copy small,
.product-price small {
    color: var(--muted-foreground);
    font-size: 11px;
}

.product-price {
    display: grid;
    justify-items: end;
    gap: 4px;
}

.product-price b {
    color: var(--primary);
    font: 700 16px var(--font-tech);
}

.large-check {
    position: static;
    width: 24px;
    height: 24px;
    border-radius: 8px;
}

.quantity-editor {
    display: grid;
    grid-template-columns: auto auto 1fr auto;
    align-items: center;
    gap: 16px;
    padding: 14px 20px;
    border-top: 1px solid var(--border);
    background: color-mix(in srgb, var(--primary-soft) 36%, var(--card));
}

.quantity-editor > span,
.quantity-editor > small {
    color: var(--muted-foreground);
    font-size: 11px;
    font-weight: 650;
}

.quantity-editor > div {
    display: grid;
    grid-template-columns: 34px 64px 34px;
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 9px;
    background: var(--card);
}

.quantity-editor button,
.quantity-editor input {
    height: 34px;
    border: 0;
    background: transparent;
    color: var(--foreground);
}

.quantity-editor button {
    display: grid;
    place-items: center;
    cursor: pointer;
}

.quantity-editor button:hover {
    background: var(--secondary);
    color: var(--primary);
}

.quantity-editor input {
    width: 64px;
    border-right: 1px solid var(--border);
    border-left: 1px solid var(--border);
    outline: 0;
    font: 700 13px var(--font-tech);
    text-align: center;
}

.quantity-editor input::-webkit-inner-spin-button {
    appearance: none;
}

.quantity-editor > strong {
    justify-self: end;
    color: var(--primary);
    font: 700 14px var(--font-tech);
}

.validation-message {
    margin: 16px 0 0;
    color: #d14343;
    font-size: 12px;
    font-weight: 650;
}

.compact-card {
    min-height: 245px;
    padding: 21px;
}

.compact-card .choice-icon {
    margin-bottom: 23px;
}

.branding-grid .compact-card {
    min-height: 235px;
}

.result-heading {
    margin-bottom: 27px;
}

.result-amounts {
    display: grid;
    grid-template-columns: 1.1fr 0.9fr;
    gap: 13px;
}

.result-amounts article {
    display: grid;
    min-height: 150px;
    align-content: center;
    gap: 9px;
    padding: 25px;
    border: 1px solid var(--border);
    border-radius: 15px;
    background: var(--secondary);
}

.result-amounts article.primary-total {
    border-color: var(--primary);
    background: linear-gradient(135deg, #006b3d, #009a59);
    color: #fff;
}

.result-amounts span,
.result-amounts small {
    color: var(--muted-foreground);
    font-size: 11px;
}

.result-amounts .primary-total span,
.result-amounts .primary-total small {
    color: rgba(255, 255, 255, 0.78);
}

.result-amounts strong {
    font: 750 clamp(27px, 3vw, 38px) var(--font-tech);
    letter-spacing: -0.04em;
}

.result-breakdown {
    margin-top: 17px;
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 15px;
}

.breakdown-heading,
.breakdown-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 24px;
    padding: 15px 20px;
    border-bottom: 1px solid var(--border);
}

.breakdown-heading {
    background: var(--secondary);
    font-size: 12px;
}

.breakdown-row:last-child {
    border-bottom: 0;
}

.breakdown-row > span {
    display: grid;
    gap: 4px;
    font-size: 13px;
    font-weight: 650;
}

.breakdown-row small {
    color: var(--muted-foreground);
    font-size: 10px;
    font-weight: 500;
}

.breakdown-row > strong,
.breakdown-heading > span {
    flex: 0 0 auto;
    color: var(--primary);
    font: 700 12px var(--font-tech);
}

.future-row {
    background: color-mix(in srgb, var(--primary-soft) 36%, var(--card));
}

.assessment-warning,
.sales-note {
    display: flex;
    align-items: flex-start;
    gap: 12px;
    margin-top: 17px;
    padding: 17px;
    border-radius: 13px;
}

.assessment-warning {
    border: 1px solid color-mix(in srgb, #d6a542 45%, var(--border));
    background: color-mix(in srgb, #fff7df 70%, var(--card));
    color: #8a681a;
    font-size: 12px;
    font-weight: 650;
}

.sales-note {
    border: 1px solid color-mix(in srgb, var(--primary) 25%, var(--border));
    background: var(--primary-soft);
}

.sales-note > div {
    display: grid;
    width: 42px;
    height: 42px;
    flex: 0 0 auto;
    place-items: center;
    border-radius: 11px;
    background: var(--primary);
    color: #fff;
}

.sales-note strong {
    color: var(--primary);
    font-size: 13px;
}

.sales-note p {
    margin: 5px 0 0;
    color: var(--muted-foreground);
    font-size: 11px;
    line-height: 1.6;
}

.excluded-box {
    margin-top: 17px;
    padding: 18px 20px;
    border: 1px solid var(--border);
    border-radius: 13px;
}

.excluded-box > strong {
    font-size: 12px;
}

.excluded-box ul {
    display: flex;
    flex-wrap: wrap;
    gap: 9px 18px;
    margin: 13px 0 0;
    padding: 0;
    list-style: none;
}

.excluded-box li {
    display: flex;
    align-items: center;
    gap: 4px;
    color: var(--muted-foreground);
    font-size: 10px;
}

.estimate-notice {
    margin: 18px 0 0;
    color: var(--muted-foreground);
    font-size: 10px;
    line-height: 1.6;
}

.result-actions {
    display: flex;
    flex-wrap: wrap;
    gap: 9px;
    margin-top: 24px;
}

.result-action,
.back-button,
.next-button {
    display: inline-flex;
    min-height: 42px;
    align-items: center;
    justify-content: center;
    gap: 8px;
    padding: 0 16px;
    border: 1px solid var(--border);
    border-radius: 10px;
    background: var(--card);
    color: var(--foreground);
    cursor: pointer;
    font: 700 12px var(--font-ui);
}

.result-action.primary,
.next-button {
    border-color: var(--primary);
    background: var(--primary);
    color: #fff;
}

.step-navigation {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-top: 32px;
    padding-top: 23px;
    border-top: 1px solid var(--border);
}

.edit-navigation {
    justify-content: flex-start;
}

.estimate-sidebar {
    position: sticky;
    top: 100px;
}

.summary-card {
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 17px;
    background: var(--card);
    box-shadow: 0 24px 70px rgba(24, 24, 27, 0.07);
}

.summary-header {
    display: flex;
    height: 58px;
    align-items: center;
    justify-content: space-between;
    padding: 0 20px;
    border-bottom: 1px solid var(--border);
    color: var(--primary);
    font: 700 10px var(--font-tech);
    letter-spacing: 0.09em;
}

.summary-total {
    display: grid;
    gap: 8px;
    padding: 25px 20px 22px;
    background: linear-gradient(145deg, var(--primary-soft), var(--card) 78%);
}

.summary-total small,
.summary-total span {
    color: var(--muted-foreground);
    font-size: 10px;
}

.summary-total strong {
    color: var(--primary);
    font: 750 29px var(--font-tech);
    letter-spacing: -0.045em;
}

.summary-meta {
    display: grid;
    margin: 0;
    padding: 15px 20px;
    border-top: 1px solid var(--border);
    border-bottom: 1px solid var(--border);
}

.summary-meta div {
    display: flex;
    justify-content: space-between;
    gap: 15px;
    padding: 6px 0;
}

.summary-meta dt,
.summary-meta dd {
    margin: 0;
    font-size: 10px;
}

.summary-meta dt {
    color: var(--muted-foreground);
}

.summary-meta dd {
    max-width: 175px;
    font-weight: 700;
    text-align: right;
}

.summary-products {
    padding: 18px 20px;
    border-bottom: 1px solid var(--border);
}

.summary-products > span {
    color: var(--muted-foreground);
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.06em;
}

.summary-products > div {
    display: grid;
    gap: 9px;
    margin-top: 13px;
}

.summary-products p {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin: 0;
}

.summary-products strong {
    font-size: 11px;
}

.summary-products small {
    color: var(--primary);
    font: 700 9px var(--font-tech);
}

.summary-products .empty-products {
    margin-top: 12px;
    color: var(--muted-foreground);
    font-size: 10px;
}

.summary-recurring {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    padding: 17px 20px;
}

.summary-recurring span {
    color: var(--muted-foreground);
    font-size: 10px;
}

.summary-recurring strong {
    font: 700 13px var(--font-tech);
}

.summary-note {
    margin: 0;
    padding: 14px 20px;
    border-top: 1px solid var(--border);
    color: var(--muted-foreground);
    font-size: 9px;
    line-height: 1.55;
}

.feedback-toast {
    position: fixed;
    z-index: 100;
    right: 28px;
    bottom: 28px;
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 12px 16px;
    border-radius: 11px;
    background: var(--primary);
    box-shadow: 0 16px 40px rgba(0, 127, 73, 0.25);
    color: #fff;
    font-size: 12px;
    font-weight: 700;
}

.step-fade-enter-active,
.step-fade-leave-active {
    transition: opacity 140ms ease, transform 140ms ease;
}

.step-fade-enter-from {
    opacity: 0;
    transform: translateX(8px);
}

.step-fade-leave-to {
    opacity: 0;
    transform: translateX(-8px);
}

.feedback-enter-active,
.feedback-leave-active {
    transition: opacity 160ms ease, transform 160ms ease;
}

.feedback-enter-from,
.feedback-leave-to {
    opacity: 0;
    transform: translateY(8px);
}

:global(html[data-theme='dark'] .calculator-page) {
    --background: #09090b;
    --foreground: #fafafa;
    --card: #101014;
    --secondary: #18181b;
    --muted-foreground: #a1a1aa;
    --border: #29292e;
    --primary: #19b66e;
    --primary-bright: #2bc77d;
    --primary-soft: #072c20;
}

:global(html[data-theme='dark'] .assessment-warning) {
    background: #302711;
    color: #e6c567;
}

@media (max-width: 1080px) {
    .embedded .calculator-layout {
        grid-template-columns: 1fr;
    }

    .embedded .estimate-sidebar {
        display: none;
    }

    .calculator-layout {
        grid-template-columns: minmax(0, 1fr) 290px;
    }

    .step-panel {
        padding: 36px;
    }

    .stepper button {
        grid-template-columns: 28px 1fr;
        padding: 11px 8px;
    }

    .stepper button strong {
        font-size: 9px;
    }

    .three-columns {
        grid-template-columns: 1fr;
    }

    .compact-card {
        min-height: 190px;
    }
}

@media (max-width: 860px) {
    .calculator-shell {
        width: min(100% - 32px, 720px);
    }

    .calculator-hero {
        padding: 54px 0 30px;
    }

    .hero-inner {
        display: grid;
        gap: 28px;
    }

    .catalog-control {
        width: 100%;
    }

    .calculator-layout {
        display: block;
    }

    .stepper {
        display: none;
    }

    .step-mobile-progress {
        display: grid;
        gap: 7px;
        padding: 16px 18px;
        border: 1px solid var(--border);
        border-radius: 14px 14px 0 0;
        background: var(--card);
    }

    .step-mobile-progress span {
        color: var(--primary);
        font: 9px var(--font-tech);
    }

    .step-mobile-progress strong {
        font-size: 13px;
    }

    .step-mobile-progress i {
        height: 3px;
        overflow: hidden;
        border-radius: 3px;
        background: var(--secondary);
    }

    .step-mobile-progress b {
        display: block;
        height: 100%;
        border-radius: inherit;
        background: var(--primary);
        transition: width 180ms ease;
    }

    .step-panel {
        min-height: auto;
        padding: 30px;
        border-top: 0;
    }

    .step-content {
        min-height: 0;
    }

    .estimate-sidebar {
        position: static;
        margin-top: 18px;
    }

    .summary-card {
        display: grid;
        grid-template-columns: 1fr 1fr;
    }

    .summary-header,
    .summary-note {
        grid-column: 1 / -1;
    }

    .summary-meta,
    .summary-products {
        border-top: 1px solid var(--border);
    }
}

@media (max-width: 620px) {
    .calculator-shell {
        width: min(100% - 24px, 520px);
    }

    .hero-copy h1 {
        font-size: 41px;
    }

    .hero-copy > p {
        font-size: 14px;
    }

    .deployment-note {
        align-items: flex-start;
        font-size: 10px;
        line-height: 1.5;
    }

    .step-panel {
        padding: 24px 18px;
    }

    .two-columns,
    .result-amounts {
        grid-template-columns: 1fr;
    }

    .tall-card,
    .license-card {
        min-height: 0;
    }

    .product-toggle {
        grid-template-columns: 26px 42px 1fr 24px;
        gap: 10px;
        padding: 16px 14px;
    }

    .product-icon {
        width: 42px;
        height: 42px;
    }

    .product-price {
        grid-column: 2 / 4;
        grid-row: 2;
        justify-items: start;
    }

    .large-check {
        grid-column: 4;
        grid-row: 1;
    }

    .quantity-editor {
        grid-template-columns: 1fr auto;
        gap: 10px;
        padding: 14px;
    }

    .quantity-editor > small {
        grid-column: 1;
        grid-row: 2;
    }

    .quantity-editor > strong {
        grid-column: 2;
        grid-row: 2;
    }

    .summary-card {
        display: block;
    }

    .result-actions,
    .result-action {
        width: 100%;
    }

    .step-navigation {
        gap: 10px;
    }

    .back-button,
    .next-button {
        flex: 1;
    }

    .feedback-toast {
        right: 12px;
        bottom: 12px;
        left: 12px;
        justify-content: center;
    }
}

@media print {
    .calculator-grid-background,
    .calculator-hero,
    .stepper,
    .step-mobile-progress,
    .step-navigation,
    .estimate-sidebar,
    .result-actions,
    .feedback-toast {
        display: none !important;
    }

    .calculator-page {
        background: #fff;
    }

    .calculator-layout,
    .calculator-shell {
        display: block;
        width: 100%;
        padding: 0;
    }

    .step-panel {
        padding: 0;
        border: 0;
        box-shadow: none;
    }

    .step-content {
        display: none;
    }

    .step-content:has(.result-heading) {
        display: block;
    }
}
</style>
