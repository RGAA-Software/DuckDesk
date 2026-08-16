<script setup lang="ts">
import { useI18n } from 'vue-i18n'
import { processSteps } from '../data/content'
import SectionHead from './SectionHead.vue'

const { t } = useI18n()
</script>

<template>
  <section id="process" class="px-section">
    <div class="container">
      <SectionHead
        :eyebrow="t('process.eyebrow')"
        :title="t('process.title')"
        :desc="t('process.desc')"
      />

      <div class="process-row">
        <div v-for="(step, i) in processSteps" :key="step.key" class="process-step">
          <span class="step-num px-mono">0{{ i + 1 }}</span>
          <h3>{{ t(`process.steps.${step.key}.title`) }}</h3>
          <p>{{ t(`process.steps.${step.key}.desc`) }}</p>
        </div>
      </div>
    </div>
  </section>
</template>

<style scoped>
.process-row {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 18px;
  margin-top: 48px;
}

.process-step {
  position: relative;
  padding: 22px 20px;
  border: 1px solid var(--px-line);
  border-top: 3px solid var(--px-green);
  background: var(--px-surface);
}

.process-step:not(:last-child)::after {
  content: '→';
  position: absolute;
  right: -16px;
  top: 50%;
  transform: translateY(-50%);
  font-family: var(--px-mono);
  color: var(--px-green);
  font-size: 18px;
}

.step-num {
  font-size: 22px;
  font-weight: 700;
  color: var(--px-green);
}

.process-step h3 {
  margin-top: 12px;
  font-size: 16px;
}

.process-step p {
  margin-top: 8px;
  color: var(--px-gray);
  font-size: 13.5px;
  line-height: 1.7;
}

@media (max-width: 1080px) {
  .process-row {
    grid-template-columns: repeat(2, 1fr);
    gap: 26px 18px;
  }

  .process-step:nth-child(2)::after {
    display: none;
  }
}

@media (max-width: 640px) {
  .process-row {
    grid-template-columns: 1fr;
  }

  .process-step::after {
    display: none;
  }
}
</style>
