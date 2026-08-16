<script setup lang="ts">
import { useI18n } from 'vue-i18n'
import { cooperation } from '../data/content'
import PxIcon from './PxIcon.vue'
import SectionHead from './SectionHead.vue'

const { t } = useI18n()
</script>

<template>
  <section id="cooperation" class="px-section">
    <div class="container">
      <SectionHead
        :eyebrow="t('cooperation.eyebrow')"
        :title="t('cooperation.title')"
        :desc="t('cooperation.desc')"
      />

      <div class="coop-grid">
        <article v-for="c in cooperation" :key="c.key" class="px-card coop-card">
          <div class="coop-icon">
            <PxIcon :art="c.icon" :size="36" />
          </div>
          <h3>{{ t(`cooperation.items.${c.key}.title`) }}</h3>
          <p class="coop-desc">{{ t(`cooperation.items.${c.key}.desc`) }}</p>
          <p class="coop-suited">{{ t(`cooperation.items.${c.key}.suited`) }}</p>
        </article>
      </div>
    </div>
  </section>
</template>

<style scoped>
.coop-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 22px;
  margin-top: 48px;
}

.coop-card {
  display: flex;
  flex-direction: column;
  padding: 28px 24px;
}

.coop-icon {
  display: grid;
  place-items: center;
  width: 48px;
  height: 48px;
  color: var(--px-green);
  background: var(--px-green-bg);
  border: 1px solid rgba(0, 185, 107, 0.3);
}

.coop-card h3 {
  margin-top: 18px;
  font-size: 18px;
}

.coop-desc {
  flex: 1;
  margin-top: 10px;
  color: var(--px-gray);
  font-size: 14px;
  line-height: 1.9;
}

.coop-suited {
  margin-top: 16px;
  padding-top: 14px;
  border-top: 1px dashed var(--px-line);
  font-size: 13px;
  color: var(--px-green-strong);
}

@media (max-width: 1080px) {
  .coop-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 640px) {
  .coop-grid {
    grid-template-columns: 1fr;
  }
}
</style>
