<script setup lang="ts">
import { useI18n } from 'vue-i18n'
import { industries } from '../data/content'
import PxIcon from './PxIcon.vue'
import SectionHead from './SectionHead.vue'

const { t } = useI18n()
</script>

<template>
  <section id="industries" class="px-section px-section-alt">
    <div class="container">
      <SectionHead
        :eyebrow="t('industries.eyebrow')"
        :title="t('industries.title')"
        :desc="t('industries.desc')"
      />

      <div class="industries-grid">
        <article v-for="it in industries" :key="it.key" class="px-card industry-card">
          <div class="industry-icon">
            <PxIcon :art="it.icon" :size="36" />
          </div>
          <h3>{{ t(`industries.items.${it.key}.title`) }}</h3>
          <p>{{ t(`industries.items.${it.key}.desc`) }}</p>
        </article>
      </div>
    </div>
  </section>
</template>

<style scoped>
.industries-grid {
  display: grid;
  grid-template-columns: repeat(5, 1fr);
  gap: 18px;
  margin-top: 48px;
}

.industry-card {
  padding: 26px 20px;
}

.industry-icon {
  color: var(--px-green);
}

.industry-card h3 {
  margin-top: 16px;
  font-size: 17px;
}

.industry-card p {
  margin-top: 8px;
  color: var(--px-gray);
  font-size: 13px;
  line-height: 1.7;
}

@media (max-width: 1080px) {
  .industries-grid {
    grid-template-columns: repeat(3, 1fr);
  }
}

@media (max-width: 640px) {
  .industries-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}
</style>
