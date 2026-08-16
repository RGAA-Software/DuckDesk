<script setup lang="ts">
import { useI18n } from 'vue-i18n'
import { servicePointCount, services } from '../data/content'
import PxIcon from './PxIcon.vue'
import SectionHead from './SectionHead.vue'

const { t } = useI18n()
</script>

<template>
  <section id="services" class="px-section">
    <div class="container">
      <SectionHead
        :eyebrow="t('services.eyebrow')"
        :title="t('services.title')"
        :desc="t('services.desc')"
      />

      <div class="services-grid">
        <article v-for="s in services" :key="s.key" class="px-card service-card">
          <div class="service-icon">
            <PxIcon :art="s.icon" />
          </div>
          <h3>{{ t(`services.items.${s.key}.title`) }}</h3>
          <p class="service-lead">{{ t(`services.items.${s.key}.lead`) }}</p>
          <ul class="service-points">
            <li v-for="i in servicePointCount" :key="i">
              {{ t(`services.items.${s.key}.points.${i - 1}`) }}
            </li>
          </ul>
        </article>
      </div>
    </div>
  </section>
</template>

<style scoped>
.services-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 22px;
  margin-top: 48px;
}

.service-card {
  padding: 28px 24px;
}

.service-icon {
  display: grid;
  place-items: center;
  width: 54px;
  height: 54px;
  color: var(--px-green);
  background: var(--px-green-bg);
  border: 1px solid rgba(0, 185, 107, 0.3);
}

.service-card h3 {
  margin-top: 20px;
  font-size: 19px;
}

.service-lead {
  margin-top: 10px;
  min-height: 50px;
  color: var(--px-gray);
  font-size: 14px;
  line-height: 1.8;
}

.service-points {
  display: flex;
  flex-direction: column;
  gap: 9px;
  margin-top: 16px;
}

.service-points li {
  display: flex;
  align-items: center;
  gap: 9px;
  font-size: 14px;
}

.service-points li::before {
  content: '';
  flex: none;
  width: 7px;
  height: 7px;
  background: var(--px-green);
}

@media (max-width: 1080px) {
  .services-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 640px) {
  .services-grid {
    grid-template-columns: 1fr;
  }
}
</style>
