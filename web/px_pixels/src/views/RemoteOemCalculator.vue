<script setup lang="ts">
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import {
    IconArrowRight,
    IconCheck,
    IconMinus,
    IconPlus,
    IconShieldCheck,
} from '@tabler/icons-vue'

import ContactUs from '@/components/ContactUs.vue'
import {
    calculateRemoteOem,
    formatRemoteOemMoney,
    remoteOemCatalog,
    remoteOemPriceBook,
} from '@/pricing/remoteOem'
import type { PricingCurrency } from '@/pricing/types'

const { locale, t, tm } = useI18n()
const currency = ref<PricingCurrency>(locale.value === 'en' ? 'USD' : 'CNY')
const concurrentStreams = ref(50)
const contactVisible = ref(false)

const estimate = computed(() =>
    calculateRemoteOem(concurrentStreams.value, currency.value),
)
const includedItems = computed(
    () => tm('remoteOemCalculator.includedItems') as string[],
)
const contactContent = computed(() =>
    [
        t('remoteOemCalculator.title'),
        `${t('remoteOemCalculator.concurrentStreams')}: ${estimate.value.concurrentStreams}`,
        `${t('remoteOemCalculator.annualLicense')}: ${money(estimate.value.annualLicenseAmount)}`,
        `${t('remoteOemCalculator.onboarding')}: ${money(estimate.value.onboardingAmount)}`,
        `${t('remoteOemCalculator.firstYear')}: ${money(estimate.value.firstYearAmount)}`,
    ].join('\n'),
)

function money(amount: number): string {
    return formatRemoteOemMoney(amount, currency.value)
}

function updateStreams(nextStreams: number) {
    if (!Number.isFinite(nextStreams)) return
    concurrentStreams.value = Math.max(1, Math.floor(nextStreams))
}

function adjustStreams(increment: number) {
    updateStreams(concurrentStreams.value + increment)
}
</script>

<template>
  <ContactUs
    v-model="contactVisible"
    :initial-title="t('remoteOemCalculator.contactTitle')"
    :initial-content="contactContent"
    initial-consult-type="enterprise"
  />

  <div class="remote-oem-calculator">
    <header>
      <div>
        <span>{{ t('remoteOemCalculator.eyebrow') }}</span>
        <h2>{{ t('remoteOemCalculator.title') }}</h2>
        <p>{{ t('remoteOemCalculator.description') }}</p>
      </div>
      <div class="currency-switch" :aria-label="t('remoteOemCalculator.currency')">
        <button
          v-for="currencyOption in (['CNY', 'USD'] as PricingCurrency[])"
          :key="currencyOption"
          type="button"
          :class="{ active: currency === currencyOption }"
          @click="currency = currencyOption"
        >
          {{ currencyOption === 'CNY' ? 'CNY ¥' : 'USD $' }}
        </button>
      </div>
    </header>

    <div class="calculator-layout">
      <section class="configuration-panel">
        <div class="field-heading">
          <div>
            <span>01</span>
            <strong>{{ t('remoteOemCalculator.concurrentStreams') }}</strong>
          </div>
          <small>{{ t('remoteOemCalculator.concurrentDescription') }}</small>
        </div>

        <div class="quantity-control">
          <button
            type="button"
            :aria-label="t('remoteOemCalculator.decrease')"
            @click="adjustStreams(-10)"
          >
            <IconMinus :size="18" :stroke-width="2" />
          </button>
          <label>
            <input
              :value="concurrentStreams"
              type="number"
              min="1"
              inputmode="numeric"
              @input="updateStreams(Number(($event.target as HTMLInputElement).value))"
            >
            <span>STREAMS</span>
          </label>
          <button
            type="button"
            :aria-label="t('remoteOemCalculator.increase')"
            @click="adjustStreams(10)"
          >
            <IconPlus :size="18" :stroke-width="2" />
          </button>
        </div>

        <div class="rate-summary">
          <div>
            <span>{{ t('remoteOemCalculator.streamRate') }}</span>
            <strong>{{ money(estimate.annualStreamPrice) }}</strong>
            <small>{{ t('remoteOemCalculator.perStreamYear') }}</small>
          </div>
          <div>
            <span>{{ t('remoteOemCalculator.annualMinimum') }}</span>
            <strong>{{ money(remoteOemCatalog[currency].annualMinimum) }}</strong>
            <small>{{ t('remoteOemCalculator.perYear') }}</small>
          </div>
        </div>

        <div class="included-box">
          <strong>{{ t('remoteOemCalculator.includedTitle') }}</strong>
          <ul>
            <li v-for="includedItem in includedItems" :key="includedItem">
              <IconCheck :size="14" :stroke-width="2" />{{ includedItem }}
            </li>
          </ul>
        </div>
      </section>

      <section class="estimate-panel">
        <div class="estimate-total">
          <span>{{ t('remoteOemCalculator.firstYear') }}</span>
          <strong>{{ money(estimate.firstYearAmount) }}</strong>
          <small>{{ t('remoteOemCalculator.publicEstimate') }}</small>
        </div>

        <dl>
          <div>
            <dt>
              {{ t('remoteOemCalculator.annualLicense') }}
              <small>
                {{ estimate.concurrentStreams }} ×
                {{ money(estimate.annualStreamPrice) }}
              </small>
            </dt>
            <dd>{{ money(estimate.annualLicenseAmount) }}</dd>
          </div>
          <div v-if="estimate.minimumAdjustmentAmount > 0">
            <dt>{{ t('remoteOemCalculator.minimumApplied') }}</dt>
            <dd>{{ money(estimate.minimumAdjustmentAmount) }}</dd>
          </div>
          <div>
            <dt>
              {{ t('remoteOemCalculator.onboarding') }}
              <small>{{ t('remoteOemCalculator.oneTime') }}</small>
            </dt>
            <dd>{{ money(estimate.onboardingAmount) }}</dd>
          </div>
          <div>
            <dt>
              {{ t('remoteOemCalculator.renewal') }}
              <small>{{ t('remoteOemCalculator.renewalDescription') }}</small>
            </dt>
            <dd>{{ money(estimate.renewalAmount) }}</dd>
          </div>
        </dl>

        <div class="estimate-note">
          <IconShieldCheck :size="19" :stroke-width="1.7" />
          <span>
            <strong>{{ t('remoteOemCalculator.annualOnly') }}</strong>
            <small>{{ t('remoteOemCalculator.exclusions') }}</small>
          </span>
        </div>

        <button class="contact-button" type="button" @click="contactVisible = true">
          {{ t('remoteOemCalculator.contact') }}
          <IconArrowRight :size="17" :stroke-width="1.9" />
        </button>
        <p class="price-book">
          {{ t('remoteOemCalculator.priceBook') }}
          {{ remoteOemPriceBook.version }} · {{ remoteOemPriceBook.effectiveFrom }}
        </p>
      </section>
    </div>
  </div>
</template>

<style scoped>
.remote-oem-calculator {
    --oem-primary: #008a50;
    --oem-primary-soft: #e8f8f0;
    --oem-border: #e4e4e7;
    --oem-card: #ffffff;
    --oem-secondary: #f5f6f7;
    --oem-text: #18181b;
    --oem-muted: #71717a;
    padding: 8px 4px 4px;
    color: var(--oem-text);
}

header {
    display: flex;
    align-items: flex-end;
    justify-content: space-between;
    gap: 32px;
    padding: 8px 8px 24px;
}

header > div:first-child {
    max-width: 740px;
}

header span {
    color: var(--oem-primary);
    font: 700 10px var(--font-tech);
    letter-spacing: 0.14em;
}

h2 {
    margin: 8px 0 7px;
    font-size: clamp(25px, 3vw, 37px);
    line-height: 1.15;
}

header p {
    margin: 0;
    color: var(--oem-muted);
    font-size: 12px;
    line-height: 1.65;
}

.currency-switch {
    display: flex;
    flex: 0 0 auto;
    padding: 3px;
    border: 1px solid var(--oem-border);
    border-radius: 10px;
    background: var(--oem-secondary);
}

.currency-switch button {
    min-height: 34px;
    padding: 0 13px;
    border: 0;
    border-radius: 7px;
    background: transparent;
    color: var(--oem-muted);
    cursor: pointer;
    font-size: 11px;
    font-weight: 750;
}

.currency-switch button.active {
    background: var(--oem-card);
    box-shadow: 0 4px 14px rgba(15, 23, 42, 0.08);
    color: var(--oem-primary);
}

.calculator-layout {
    display: grid;
    grid-template-columns: 1.05fr 0.95fr;
    overflow: hidden;
    border: 1px solid var(--oem-border);
    border-radius: 18px;
}

.configuration-panel,
.estimate-panel {
    padding: 28px;
    background: var(--oem-card);
}

.estimate-panel {
    border-left: 1px solid var(--oem-border);
    background: var(--oem-secondary);
}

.field-heading > div {
    display: flex;
    align-items: center;
    gap: 11px;
}

.field-heading span {
    color: var(--oem-primary);
    font: 700 10px var(--font-tech);
}

.field-heading strong {
    font-size: 16px;
}

.field-heading small {
    display: block;
    margin-top: 7px;
    color: var(--oem-muted);
    font-size: 11px;
}

.quantity-control {
    display: grid;
    grid-template-columns: 44px minmax(120px, 1fr) 44px;
    gap: 8px;
    margin-top: 20px;
}

.quantity-control > button {
    display: grid;
    place-items: center;
    border: 1px solid var(--oem-border);
    border-radius: 10px;
    background: var(--oem-card);
    color: var(--oem-primary);
    cursor: pointer;
}

.quantity-control label {
    display: flex;
    min-height: 52px;
    align-items: center;
    justify-content: center;
    gap: 10px;
    border: 1px solid var(--oem-primary);
    border-radius: 10px;
    background: var(--oem-primary-soft);
}

.quantity-control input {
    width: 86px;
    border: 0;
    outline: 0;
    background: transparent;
    color: var(--oem-text);
    font-size: 24px;
    font-weight: 800;
    text-align: right;
}

.quantity-control label span {
    color: var(--oem-primary);
    font: 700 9px var(--font-tech);
    letter-spacing: 0.1em;
}

.rate-summary {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 9px;
    margin-top: 12px;
}

.rate-summary > div {
    padding: 14px;
    border: 1px solid var(--oem-border);
    border-radius: 11px;
}

.rate-summary span,
.rate-summary small {
    display: block;
    color: var(--oem-muted);
    font-size: 9px;
}

.rate-summary strong {
    display: block;
    margin: 5px 0 3px;
    font-size: 15px;
}

.included-box {
    margin-top: 16px;
    padding: 17px;
    border-radius: 12px;
    background: var(--oem-secondary);
}

.included-box > strong {
    font-size: 12px;
}

.included-box ul {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 8px 14px;
    margin: 12px 0 0;
    padding: 0;
    list-style: none;
}

.included-box li {
    display: flex;
    align-items: center;
    gap: 6px;
    color: var(--oem-muted);
    font-size: 10px;
}

.included-box svg {
    flex: 0 0 auto;
    color: var(--oem-primary);
}

.estimate-total span,
.estimate-total small {
    display: block;
    color: var(--oem-muted);
    font-size: 10px;
}

.estimate-total strong {
    display: block;
    margin: 5px 0;
    color: var(--oem-primary);
    font-size: clamp(30px, 4vw, 44px);
    letter-spacing: -0.04em;
}

dl {
    margin: 18px 0 0;
    border-block: 1px solid var(--oem-border);
}

dl > div {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 20px;
    min-height: 55px;
    border-bottom: 1px solid var(--oem-border);
}

dl > div:last-child {
    border-bottom: 0;
}

dt {
    font-size: 11px;
    font-weight: 700;
}

dt small {
    display: block;
    margin-top: 3px;
    color: var(--oem-muted);
    font-size: 9px;
    font-weight: 400;
}

dd {
    margin: 0;
    font-size: 12px;
    font-weight: 800;
}

.estimate-note {
    display: flex;
    gap: 10px;
    margin-top: 16px;
    padding: 13px;
    border-radius: 10px;
    background: var(--oem-primary-soft);
    color: var(--oem-primary);
}

.estimate-note svg {
    flex: 0 0 auto;
}

.estimate-note strong,
.estimate-note small {
    display: block;
}

.estimate-note strong {
    font-size: 11px;
}

.estimate-note small {
    margin-top: 4px;
    color: var(--oem-muted);
    font-size: 9px;
    line-height: 1.5;
}

.contact-button {
    display: flex;
    width: 100%;
    min-height: 42px;
    align-items: center;
    justify-content: center;
    gap: 8px;
    margin-top: 14px;
    border: 0;
    border-radius: 10px;
    background: var(--oem-primary);
    color: #ffffff;
    cursor: pointer;
    font-size: 12px;
    font-weight: 750;
}

.price-book {
    margin: 8px 0 0;
    color: var(--oem-muted);
    font: 9px var(--font-tech);
    text-align: center;
}

:global(html[data-theme='dark']) .remote-oem-calculator {
    --oem-primary: #20c982;
    --oem-primary-soft: #073c2d;
    --oem-border: #2a2a31;
    --oem-card: #111115;
    --oem-secondary: #18181d;
    --oem-text: #fafafa;
    --oem-muted: #a1a1aa;
}

@media (max-width: 760px) {
    header {
        align-items: flex-start;
        flex-direction: column;
    }

    .calculator-layout {
        grid-template-columns: 1fr;
    }

    .estimate-panel {
        border-top: 1px solid var(--oem-border);
        border-left: 0;
    }
}
</style>
