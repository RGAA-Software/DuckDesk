<script setup lang="ts">
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { ElNotification } from 'element-plus'
import type { FormInstance, FormRules } from 'element-plus'
import axiosHttp from '@/http.ts'

const { t } = useI18n()

interface Consult {
  title: string
  yourName: string
  consultType: string
  content: string
  email: string
  wechat: string
  qq: string
}

const emptyConsult = (): Consult => ({
  title: '',
  yourName: '',
  consultType: '',
  content: '',
  email: '',
  wechat: '',
  qq: '',
})

const consult = ref<Consult>(emptyConsult())
const formRef = ref<FormInstance>()
const submitting = ref(false)

const rules = computed<FormRules<Consult>>(() => ({
  title: [{ required: true, message: t('consult.required'), trigger: 'blur' }],
  yourName: [{ required: true, message: t('consult.required'), trigger: 'blur' }],
  consultType: [{ required: true, message: t('consult.required'), trigger: 'change' }],
  content: [{ required: true, message: t('consult.required'), trigger: 'blur' }],
  email: [
    { required: true, message: t('consult.required'), trigger: 'blur' },
    { type: 'email', message: t('consult.emailInvalid'), trigger: 'blur' },
  ],
}))

// 父组件传入的 v-model
const props = defineProps<{
  modelValue: boolean
}>()

const emit = defineEmits<{
  (e: 'update:modelValue', value: boolean): void
}>()

// 内部代理状态（关键）
const visible = computed({
  get: () => props.modelValue,
  set: (val) => emit('update:modelValue', val),
})

const close = () => {
  visible.value = false
}

async function confirm() {
  if (!formRef.value) return
  const valid = await formRef.value.validate().catch(() => false)
  if (!valid) return

  submitting.value = true
  try {
    await axiosHttp.post(
      '/api/v1/create/new/consult',
      {
        title: consult.value.title,
        your_name: consult.value.yourName,
        consult_type: consult.value.consultType,
        content: consult.value.content,
        email: consult.value.email,
        wechat: consult.value.wechat,
        qq: consult.value.qq,
      },
      { headers: { 'Content-Type': 'application/json' } },
    )

    ElNotification({
      title: t('consult.successTitle'),
      message: t('consult.successMessage'),
      type: 'primary',
    })
    visible.value = false
    consult.value = emptyConsult()
    formRef.value?.clearValidate()
  } catch (error) {
    console.log('create consult error:', error)
    ElNotification({
      title: t('consult.failTitle'),
      message: t('consult.failMessage'),
      type: 'warning',
    })
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <el-dialog v-model="visible" align-center class="!max-w-[92vw] !w-140">
    <template #header>
      <span class="text-lg text-slate-200">{{ t('consult.dialogTitle') }}</span>
    </template>

    <el-form ref="formRef" :model="consult" :rules="rules" label-width="auto">
      <el-form-item :label="t('consult.title')" prop="title">
        <el-input v-model="consult.title" />
      </el-form-item>

      <el-form-item :label="t('consult.yourName')" prop="yourName">
        <el-input v-model="consult.yourName" />
      </el-form-item>

      <el-form-item :label="t('consult.type')" prop="consultType">
        <el-select v-model="consult.consultType" :placeholder="t('consult.typePlaceholder')">
          <el-option :label="t('consult.typePersonal')" value="personal" />
          <el-option :label="t('consult.typeEnterprise')" value="enterprise" />
        </el-select>
      </el-form-item>

      <el-form-item :label="t('consult.content')" prop="content">
        <el-input
          v-model="consult.content"
          :rows="2"
          type="textarea"
          :placeholder="t('consult.contentPlaceholder')"
        />
      </el-form-item>

      <el-form-item :label="t('consult.email')" prop="email">
        <el-input v-model="consult.email" />
      </el-form-item>

      <el-form-item :label="t('consult.wechat')" prop="wechat">
        <el-input v-model="consult.wechat" />
      </el-form-item>

      <el-form-item :label="t('consult.qq')" prop="qq">
        <el-input v-model="consult.qq" />
      </el-form-item>
    </el-form>

    <template #footer>
      <div class="dialog-footer">
        <el-button @click="close">{{ t('consult.cancel') }}</el-button>
        <el-button type="primary" :loading="submitting" @click="confirm">
          {{ t('consult.submit') }}
        </el-button>
      </div>
    </template>
  </el-dialog>
</template>
