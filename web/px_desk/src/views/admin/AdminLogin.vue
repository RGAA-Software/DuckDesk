<script setup lang="ts">
import { ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'
import adminHttp, { setAdminToken } from '@/adminHttp.ts'
import iconLogo from '@/assets/icon/ic_trans_icon_blue.png'

const { t } = useI18n()
const router = useRouter()

const password = ref('')
const submitting = ref(false)
const errorMsg = ref('')

async function doLogin() {
  if (!password.value || submitting.value) return
  submitting.value = true
  errorMsg.value = ''
  try {
    await adminHttp.post(
      '/api/v1/admin/verify',
      { password: password.value },
      { headers: { 'Content-Type': 'application/json' } },
    )
    setAdminToken(password.value)
    router.push('/admin/panel')
  } catch {
    errorMsg.value = t('admin.loginFailed')
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <div class="flex min-h-screen items-center justify-center px-4">
    <div class="cyber-panel cyber-corners w-full max-w-sm p-8">
      <div class="flex flex-col items-center gap-4">
        <img :src="iconLogo" alt="GoDesk" class="h-14 w-14" />
        <h1 class="font-tech text-xl font-bold tracking-[0.2em] text-cyber-text">
          {{ t('admin.loginTitle') }}
        </h1>
        <div class="flex items-center gap-2">
          <span class="cyber-dot"></span>
          <span class="font-tech text-[10px] tracking-[0.18em] text-cyber-muted">RESTRICTED AREA</span>
        </div>
      </div>

      <div class="mt-8 flex flex-col gap-4">
        <label class="cyber-label">{{ t('admin.password') }}</label>
        <el-input
          v-model="password"
          type="password"
          show-password
          :placeholder="t('admin.passwordPlaceholder')"
          @keyup.enter="doLogin"
        />

        <p v-if="errorMsg" class="font-tech text-xs tracking-wider text-cyber-red">{{ errorMsg }}</p>

        <el-button type="primary" class="!h-11 w-full" :loading="submitting" @click="doLogin">
          {{ t('admin.login') }}
        </el-button>
      </div>
    </div>
  </div>
</template>
