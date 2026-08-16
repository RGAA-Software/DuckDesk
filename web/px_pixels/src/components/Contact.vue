<script setup lang="ts">
import { reactive, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { App as AntdApp } from 'ant-design-vue'
import type { FormInstance } from 'ant-design-vue'
import { contactInfo } from '../data/content'

const { t } = useI18n()
const { message } = AntdApp.useApp()
const formRef = ref<FormInstance>()

const formState = reactive({
  name: '',
  company: '',
  phone: '',
  desc: '',
})

/* 必填提示由 Ant Design 组件库语言（zh_CN / en_US）提供 */
const rules = {
  name: [{ required: true, trigger: 'blur' }],
  company: [{ required: true, trigger: 'blur' }],
  phone: [{ required: true, trigger: 'blur' }],
}

function onSubmit() {
  message.success(t('contact.success'))
  formRef.value?.resetFields()
}
</script>

<template>
  <section id="contact" class="contact">
    <div class="container contact-inner">
      <div class="contact-copy">
        <p class="contact-eyebrow px-mono">{{ t('contact.eyebrow') }}</p>
        <h2>{{ t('contact.title') }}</h2>
        <p class="contact-desc">{{ t('contact.desc') }}</p>
        <ul class="contact-list">
          <li class="contact-item">
            <b>PHONE</b>
            <span>{{ contactInfo.phone }}</span>
          </li>
          <li class="contact-item">
            <b>EMAIL</b>
            <span>{{ contactInfo.email }}</span>
          </li>
          <li class="contact-item">
            <b>ADDRESS</b>
            <span>{{ contactInfo.address }}</span>
          </li>
        </ul>
      </div>

      <div class="contact-card">
        <h3>{{ t('contact.formTitle') }}</h3>
        <p class="form-tip">{{ t('contact.formTip') }}</p>
        <a-form
          ref="formRef"
          :model="formState"
          :rules="rules"
          layout="vertical"
          @finish="onSubmit"
        >
          <a-form-item :label="t('contact.name')" name="name">
            <a-input v-model:value="formState.name" :placeholder="t('contact.placeholders.name')" />
          </a-form-item>
          <a-form-item :label="t('contact.company')" name="company">
            <a-input
              v-model:value="formState.company"
              :placeholder="t('contact.placeholders.company')"
            />
          </a-form-item>
          <a-form-item :label="t('contact.phone')" name="phone">
            <a-input v-model:value="formState.phone" :placeholder="t('contact.placeholders.phone')" />
          </a-form-item>
          <a-form-item :label="t('contact.descLabel')">
            <a-textarea
              v-model:value="formState.desc"
              :rows="3"
              :placeholder="t('contact.placeholders.desc')"
            />
          </a-form-item>
          <a-button type="primary" html-type="submit" block size="large" class="px-shadow-btn">
            {{ t('contact.submit') }}
          </a-button>
        </a-form>
      </div>
    </div>
  </section>
</template>

<style scoped>
.contact {
  position: relative;
  padding: 96px 0;
  color: #fff;
  background: linear-gradient(135deg, #00b96b 0%, #00a860 55%, #009a59 100%);
}

.contact::before {
  content: '';
  position: absolute;
  inset: 0;
  background-image: linear-gradient(
      to right,
      rgba(255, 255, 255, 0.06) 1px,
      transparent 1px
    ),
    linear-gradient(to bottom, rgba(255, 255, 255, 0.06) 1px, transparent 1px);
  background-size: 26px 26px;
}

.contact-inner {
  position: relative;
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 56px;
  align-items: center;
}

.contact-eyebrow {
  font-size: 13px;
  letter-spacing: 2px;
  opacity: 0.85;
}

.contact-copy h2 {
  margin-top: 14px;
  font-size: 32px;
  line-height: 1.4;
}

.contact-desc {
  margin-top: 16px;
  font-size: 15px;
  line-height: 2;
  opacity: 0.9;
}

.contact-list {
  display: flex;
  flex-direction: column;
  gap: 18px;
  margin-top: 34px;
}

.contact-item b {
  display: block;
  font-family: var(--px-mono);
  font-size: 12px;
  font-weight: 400;
  letter-spacing: 2px;
  opacity: 0.7;
}

.contact-item span {
  display: block;
  margin-top: 4px;
  font-size: 15px;
}

.contact-card {
  padding: 30px;
  color: var(--px-ink);
  background: var(--px-surface);
  border-radius: 2px;
  box-shadow: 0 18px 40px rgba(0, 60, 35, 0.25), 0 4px 0 rgba(0, 90, 50, 0.4);
}

.contact-card h3 {
  font-size: 18px;
}

.form-tip {
  margin: 6px 0 18px;
  font-size: 13px;
  color: var(--px-gray);
}

@media (max-width: 960px) {
  .contact {
    padding: 64px 0;
  }

  .contact-inner {
    grid-template-columns: 1fr;
    gap: 40px;
  }

  .contact-copy h2 {
    font-size: 26px;
  }
}
</style>
