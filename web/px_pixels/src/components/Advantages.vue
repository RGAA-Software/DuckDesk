<script setup lang="ts">
import { useI18n } from 'vue-i18n'
import { advantages } from '../data/content'
import PxIcon from './PxIcon.vue'
import SectionHead from './SectionHead.vue'

const { t } = useI18n()
</script>

<template>
  <section id="advantages" class="px-section px-section-alt">
    <div class="container">
      <SectionHead
        :eyebrow="t('advantages.eyebrow')"
        :title="t('advantages.title')"
        :desc="t('advantages.desc')"
      />

      <div class="adv-grid">
        <article v-for="a in advantages" :key="a.key" class="px-card adv-card">
          <div class="adv-icon">
            <PxIcon :art="a.icon" :size="34" />
          </div>
          <div>
            <h3>{{ t(`advantages.items.${a.key}.title`) }}</h3>
            <p>{{ t(`advantages.items.${a.key}.desc`) }}</p>
          </div>
        </article>
      </div>
    </div>
  </section>
</template>

<style scoped>
.adv-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 18px;
  margin-top: 48px;
}

.adv-card {
  display: flex;
  align-items: flex-start;
  gap: 16px;
  padding: 24px;
}

.adv-icon {
  flex: none;
  display: grid;
  place-items: center;
  width: 48px;
  height: 48px;
  color: var(--px-green);
  background: var(--px-green-bg);
  border: 1px solid rgba(0, 185, 107, 0.3);
}

.adv-card h3 {
  font-size: 16px;
}

.adv-card p {
  margin-top: 8px;
  color: var(--px-gray);
  font-size: 13.5px;
  line-height: 1.7;
}

@media (max-width: 1080px) {
  .adv-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 640px) {
  .adv-grid {
    grid-template-columns: 1fr;
  }
}
</style>
