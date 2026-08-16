<script setup lang="ts">
import { useI18n } from 'vue-i18n'
import { packageFeatureCount, packages } from '../data/content'
import SectionHead from './SectionHead.vue'

const { t } = useI18n()
</script>

<template>
  <section id="packages" class="px-section px-section-alt">
    <div class="container">
      <SectionHead
        :eyebrow="t('packages.eyebrow')"
        :title="t('packages.title')"
        :desc="t('packages.desc')"
      />

      <div class="pack-grid">
        <article
          v-for="p in packages"
          :key="p.key"
          class="px-card package-card"
          :class="{ featured: p.featured }"
        >
          <span v-if="p.featured" class="px-badge">{{ t('packages.badge') }}</span>
          <h3 class="package-name">{{ t(`packages.items.${p.key}.name`) }}</h3>
          <p class="package-tagline">{{ t(`packages.items.${p.key}.tagline`) }}</p>
          <div class="package-price">
            <b class="px-mono">{{ t(`packages.items.${p.key}.price`) }}</b>
          </div>
          <ul class="package-features">
            <li v-for="i in packageFeatureCount" :key="i">
              {{ t(`packages.items.${p.key}.features.${i - 1}`) }}
            </li>
          </ul>
          <a-button type="primary" block class="px-shadow-btn" href="#contact">
            {{ t('nav.contact') }}
          </a-button>
        </article>
      </div>
    </div>
  </section>
</template>

<style scoped>
.pack-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 22px;
  margin-top: 48px;
  align-items: stretch;
}

.package-card {
  position: relative;
  display: flex;
  flex-direction: column;
  padding: 30px 26px;
}

.package-card.featured {
  border-color: var(--px-green);
  box-shadow: 0 12px 32px rgba(0, 185, 107, 0.16), 0 4px 0 rgba(0, 185, 107, 0.35);
}

.px-badge {
  position: absolute;
  top: -1px;
  right: 22px;
  padding: 4px 12px;
  background: var(--px-green);
  color: #fff;
  font-size: 12px;
  letter-spacing: 1px;
}

.package-name {
  font-size: 20px;
}

.package-tagline {
  margin-top: 8px;
  color: var(--px-gray);
  font-size: 13.5px;
}

.package-price {
  margin-top: 20px;
  padding: 14px 0;
  border-top: 1px dashed var(--px-line);
  border-bottom: 1px dashed var(--px-line);
}

.package-price b {
  font-size: 14.5px;
  letter-spacing: 0.5px;
  color: var(--px-green-strong);
}

.package-features {
  display: flex;
  flex-direction: column;
  flex: 1;
  gap: 12px;
  margin: 22px 0 26px;
}

.package-features li {
  display: flex;
  align-items: center;
  gap: 9px;
  font-size: 14px;
}

.package-features li::before {
  content: '';
  flex: none;
  width: 7px;
  height: 7px;
  background: var(--px-green);
}

@media (max-width: 960px) {
  .pack-grid {
    grid-template-columns: 1fr;
    max-width: 480px;
    margin-left: auto;
    margin-right: auto;
  }
}
</style>
