<script setup lang="ts">
import { ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { faqs } from '../data/content'
import SectionHead from './SectionHead.vue'

const { t } = useI18n()
const activeKey = ref<string[]>(['0'])
</script>

<template>
  <section id="faq" class="px-section">
    <div class="container">
      <SectionHead
        :eyebrow="t('faq.eyebrow')"
        :title="t('faq.title')"
        :desc="t('faq.desc')"
      />

      <a-collapse v-model:activeKey="activeKey" accordion :bordered="false" class="faq-collapse">
        <a-collapse-panel v-for="(f, i) in faqs" :key="f.key" :header="t(`faq.items.${f.key}.q`)">
          <p class="faq-answer">{{ t(`faq.items.${f.key}.a`) }}</p>
        </a-collapse-panel>
      </a-collapse>
    </div>
  </section>
</template>

<style scoped>
.faq-collapse {
  max-width: 860px;
  margin-top: 40px;
  background: transparent;
}

.faq-answer {
  color: var(--px-gray);
  font-size: 14px;
  line-height: 1.9;
}
</style>
