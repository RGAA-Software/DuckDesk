<script lang="ts" setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRoute, useRouter } from 'vue-router'
import { ElNotification } from 'element-plus'
import type { FormInstance, FormRules } from 'element-plus'
import axiosHttp from '@/http.ts'
import ContactUs from '@/components/ContactUs.vue'
import transIcon from '@/assets/icon/ic_translate.svg'
import iconLogo from '@/assets/icon/ic_trans_icon_blue.png'
import goxrLogo from '@/assets/products/goxr-logo.png'
import cmonLogo from '@/assets/products/cybermonitor-logo.svg'
import wechatIcon from '@/assets/icon/ic_wechat.svg'
import emailIconFooter from '@/assets/icon/ic_email.svg'

const { t, locale } = useI18n()
const router = useRouter()
const route = useRoute()

// ---------- 导航 ----------
const scrolled = ref(false)
const mobileMenuVisible = ref(false)

const onScroll = () => {
  scrolled.value = window.scrollY > 8
}

onMounted(() => window.addEventListener('scroll', onScroll, { passive: true }))
onBeforeUnmount(() => window.removeEventListener('scroll', onScroll))

const navItems = computed(() => [
  { path: '/main', label: t('nav.home') },
  { path: '/price', label: t('nav.price') },
  { path: '/docs', label: t('nav.docs') },
])

const productItems = computed(() => [
  { path: '/products/godesk', label: t('productNames.godesk'), logo: iconLogo },
  { path: '/products/goxr', label: t('productNames.goxr'), logo: goxrLogo },
  { path: '/products/cybermonitor', label: t('productNames.cybermonitor'), logo: cmonLogo },
])

const isActive = (path: string) => route.path.startsWith(path)
const isProductActive = computed(() => route.path.startsWith('/products'))

const goProduct = (path: string) => {
  mobileMenuVisible.value = false
  router.push(path)
}

const goPage = (path: string) => {
  mobileMenuVisible.value = false
  router.push(path)
}

const handleTranslateClick = (command: string) => {
  localStorage.setItem('language', command)
  locale.value = command
}

// ---------- 页脚 ----------
const contactUsVisible = ref(false)
const goContactUs = () => {
  contactUsVisible.value = true
}
const goTerms = () => router.push('/terms')
const goPrivacy = () => router.push('/privacy')
const goHelp = () => router.push('/docs')

// ---------- 工单对话框 ----------
const issueVisible = ref(false)
const issueSubmitting = ref(false)
const issueFormRef = ref<FormInstance>()

interface Issue {
  title: string
  yourName: string
  desc: string
  version: string
  os: string
  email: string
  wechat: string
  qq: string
}

const emptyIssue = (): Issue => ({
  title: '',
  yourName: '',
  desc: '',
  version: '',
  os: '',
  email: '',
  wechat: '',
  qq: '',
})

const issue = ref<Issue>(emptyIssue())

const issueRules = computed<FormRules<Issue>>(() => ({
  title: [{ required: true, message: t('issue.required'), trigger: 'blur' }],
  yourName: [{ required: true, message: t('issue.required'), trigger: 'blur' }],
  desc: [{ required: true, message: t('issue.required'), trigger: 'blur' }],
}))

const goIssue = () => {
  issueVisible.value = true
}

async function confirmIssue() {
  if (!issueFormRef.value) return
  const valid = await issueFormRef.value.validate().catch(() => false)
  if (!valid) return

  issueSubmitting.value = true
  try {
    await axiosHttp.post(
      '/api/v1/create/new/issue',
      {
        title: issue.value.title,
        your_name: issue.value.yourName,
        desc: issue.value.desc,
        version: issue.value.version,
        os: issue.value.os,
        email: issue.value.email,
        wechat: issue.value.wechat,
        qq: issue.value.qq,
      },
      { headers: { 'Content-Type': 'application/json' } },
    )
    ElNotification({
      title: t('issue.successTitle'),
      message: t('issue.successMessage'),
      type: 'primary',
    })
    issueVisible.value = false
    issue.value = emptyIssue()
    issueFormRef.value?.clearValidate()
  } catch (error) {
    console.log('post issue failed: ', error)
    ElNotification({
      title: t('issue.failTitle'),
      message: t('issue.failMessage'),
      type: 'warning',
    })
  } finally {
    issueSubmitting.value = false
  }
}
</script>

<template>
  <!-- 吸顶导航栏（实色 + 底部分隔线） -->
  <header
    class="sticky top-0 z-50 border-b transition-colors duration-200"
    :class="scrolled ? 'bg-cyber-nav border-cyber-line' : 'bg-transparent border-transparent'"
  >
    <div class="section-container flex h-14 items-center justify-between">
      <!-- Logo -->
      <button class="flex items-center gap-2.5 cursor-pointer" @click="goPage('/main')">
        <img :src="iconLogo" alt="GoDesk" class="h-8 w-8" />
        <span class="font-tech text-lg font-bold tracking-[0.22em] text-cyber-text">GODESK</span>
      </button>

      <!-- 桌面端导航 -->
      <nav class="hidden md:flex items-center gap-2">
        <button
          class="nav-item font-tech"
          :class="isActive('/main') ? 'nav-item-active' : ''"
          @click="goPage('/main')"
        >
          {{ t('nav.home') }}
        </button>

        <el-dropdown trigger="hover" popper-class="product-dropdown" @command="goProduct">
          <button class="nav-item font-tech" :class="isProductActive ? 'nav-item-active' : ''">
            {{ t('nav.products') }} ▾
          </button>
          <template #dropdown>
            <el-dropdown-menu>
              <el-dropdown-item v-for="p in productItems" :key="p.path" :command="p.path">
                <img :src="p.logo" :alt="p.label" />
                <span>{{ p.label }}</span>
              </el-dropdown-item>
            </el-dropdown-menu>
          </template>
        </el-dropdown>

        <button
          v-for="item in navItems.slice(1)"
          :key="item.path"
          class="nav-item font-tech"
          :class="isActive(item.path) ? 'nav-item-active' : ''"
          @click="goPage(item.path)"
        >
          {{ item.label }}
        </button>
      </nav>

      <!-- 桌面端右侧操作区 -->
      <div class="hidden md:flex items-center gap-5">
        <el-dropdown trigger="click" @command="handleTranslateClick">
          <img :src="transIcon" alt="language" class="h-5 w-5 cursor-pointer invert opacity-70 hover:opacity-100 transition-opacity" />
          <template #dropdown>
            <el-dropdown-menu>
              <el-dropdown-item command="zh">简体中文</el-dropdown-item>
              <el-dropdown-item command="en">English</el-dropdown-item>
            </el-dropdown-menu>
          </template>
        </el-dropdown>

      </div>

      <!-- 移动端汉堡按钮 -->
      <button
        class="md:hidden flex items-center justify-center h-10 w-10 text-cyber-text hover:bg-white/5 transition-colors cursor-pointer"
        @click="mobileMenuVisible = true"
      >
        <el-icon :size="22"><Menu /></el-icon>
      </button>
    </div>
  </header>

  <!-- 移动端抽屉菜单 -->
  <el-drawer v-model="mobileMenuVisible" direction="rtl" size="260px" :with-header="false">
    <div class="drawer-nav flex flex-col gap-2 pt-6">
      <button
        class="nav-item font-tech text-left !px-4 !py-3"
        :class="isActive('/main') ? 'nav-item-active' : ''"
        @click="goPage('/main')"
      >
        {{ t('nav.home') }}
      </button>

      <div class="cyber-label px-4 pt-2">{{ t('nav.products') }}</div>
      <button
        v-for="p in productItems"
        :key="p.path"
        class="nav-item font-tech text-left !px-4 !py-3 flex items-center gap-2.5"
        :class="isActive(p.path) ? 'nav-item-active' : ''"
        @click="goProduct(p.path)"
      >
        <img :src="p.logo" :alt="p.label" class="h-4.5 w-4.5" />
        {{ p.label }}
      </button>

      <button
        v-for="item in navItems.slice(1)"
        :key="item.path"
        class="nav-item font-tech text-left !px-4 !py-3"
        :class="isActive(item.path) ? 'nav-item-active' : ''"
        @click="goPage(item.path)"
      >
        {{ item.label }}
      </button>

      <div class="divider-line my-4" />

      <div class="flex items-center gap-5 px-4">
        <el-dropdown trigger="click" @command="handleTranslateClick">
          <img :src="transIcon" alt="language" class="h-5 w-5 cursor-pointer invert opacity-70" />
          <template #dropdown>
            <el-dropdown-menu>
              <el-dropdown-item command="zh">简体中文</el-dropdown-item>
              <el-dropdown-item command="en">English</el-dropdown-item>
            </el-dropdown-menu>
          </template>
        </el-dropdown>

      </div>
    </div>
  </el-drawer>

  <!-- 页面主体 -->
  <main>
    <RouterView />
  </main>

  <!-- 页脚 -->
  <footer class="mt-20 border-t border-cyber-line bg-cyber-nav">
    <div class="section-container grid gap-10 py-12 md:grid-cols-3">
      <!-- 品牌区 -->
      <div class="flex flex-col items-center md:items-start gap-3">
        <div class="flex items-center gap-2.5">
          <img :src="iconLogo" alt="GoDesk" class="h-9 w-9" />
          <div>
            <div class="font-tech text-lg font-bold tracking-[0.22em] text-cyber-text leading-5">GODESK</div>
            <div class="font-tech text-[10px] tracking-[0.18em] text-cyber-brand">{{ t('footer.slogan') }}</div>
          </div>
        </div>
        <div class="flex gap-4 text-sm">
          <el-link class="!text-cyber-muted hover:!text-cyber-text" :underline="false" @click="goTerms">
            {{ t('footer.terms') }}
          </el-link>
          <el-link class="!text-cyber-muted hover:!text-cyber-text" :underline="false" @click="goPrivacy">
            {{ t('footer.privacy') }}
          </el-link>
        </div>
        <!-- 状态行 -->
        <div class="mt-2 flex items-center gap-2 border border-cyber-line bg-cyber-bg2 px-3 py-1.5">
          <span class="cyber-dot"></span>
          <span class="font-tech text-[10px] tracking-[0.14em] text-cyber-muted">SYS.STATUS: ONLINE</span>
        </div>
      </div>

      <!-- 合作与支持 -->
      <div class="flex flex-col items-center md:items-start gap-2">
        <div class="cyber-label mb-2">{{ t('footer.support') }}</div>
        <el-link type="primary" class="h-6" :underline="false" @click="goContactUs">
          {{ t('footer.contactUs') }}
        </el-link>
        <el-link class="h-6 !text-cyber-muted hover:!text-cyber-text" :underline="false" @click="goHelp">
          {{ t('footer.helpCenter') }}
        </el-link>
        <el-link class="h-6 !text-cyber-muted hover:!text-cyber-text" :underline="false" @click="goIssue">
          {{ t('footer.ticket') }}
        </el-link>
      </div>

      <!-- 联系方式 -->
      <div class="flex flex-col items-center md:items-start gap-2">
        <div class="cyber-label mb-2">{{ t('footer.contactUs') }}</div>
        <div class="flex items-center gap-2">
          <img :src="emailIconFooter" alt="" class="h-4 w-4 footer-icon" />
          <span class="font-tech text-sm text-cyber-muted">{{ t('consult.email') }}: godesk-sales@outlook.com</span>
        </div>
        <div class="flex items-center gap-2">
          <img :src="wechatIcon" alt="" class="h-4.5 w-4.5 footer-icon" />
          <span class="font-tech text-sm text-cyber-muted">{{ t('consult.wechat') }}: AlmostDawn</span>
        </div>
      </div>
    </div>
  </footer>

  <ContactUs v-model="contactUsVisible" />

  <!-- 提交工单对话框 -->
  <el-dialog v-model="issueVisible" align-center class="!max-w-[92vw] !w-140">
    <template #header>
      <span class="font-tech text-base tracking-wider text-cyber-text">{{ t('issue.dialogTitle') }}</span>
    </template>

    <el-form ref="issueFormRef" :model="issue" :rules="issueRules" label-width="auto">
      <el-form-item :label="t('issue.title')" prop="title">
        <el-input v-model="issue.title" />
      </el-form-item>

      <el-form-item :label="t('issue.yourName')" prop="yourName">
        <el-input v-model="issue.yourName" />
      </el-form-item>

      <el-form-item :label="t('issue.desc')" prop="desc">
        <el-input
          v-model="issue.desc"
          :rows="2"
          type="textarea"
          :placeholder="t('issue.descPlaceholder')"
        />
      </el-form-item>

      <el-form-item :label="t('issue.version')" prop="version">
        <el-input v-model="issue.version" />
      </el-form-item>

      <el-form-item :label="t('issue.os')" prop="os">
        <el-input v-model="issue.os" />
      </el-form-item>

      <el-form-item :label="t('issue.email')" prop="email">
        <el-input v-model="issue.email" />
      </el-form-item>

      <el-form-item :label="t('issue.wechat')" prop="wechat">
        <el-input v-model="issue.wechat" />
      </el-form-item>

      <el-form-item :label="t('issue.qq')" prop="qq">
        <el-input v-model="issue.qq" />
      </el-form-item>
    </el-form>

    <template #footer>
      <div class="dialog-footer">
        <el-button @click="issueVisible = false">{{ t('issue.cancel') }}</el-button>
        <el-button type="primary" :loading="issueSubmitting" @click="confirmIssue">
          {{ t('issue.submit') }}
        </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<style scoped>
/* 导航项：等宽大写小字；激活 = 实心蓝切角块；统一高度 */
.nav-item {
  height: 32px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 0 18px;
  font-size: 12px;
  letter-spacing: 0.12em;
  text-transform: uppercase;
  color: #adb5ae;
  background: transparent;
  cursor: pointer;
  clip-path: polygon(9px 0, 100% 0, 100% calc(100% - 9px), calc(100% - 9px) 100%, 0 100%, 0 9px);
  transition: background 0.15s, color 0.15s;
}
/* 产品下拉触发器与直排导航项高度对齐 */
:deep(.el-dropdown) {
  display: inline-flex;
  align-items: stretch;
}
/* 抽屉内导航项高度自适应 */
.drawer-nav .nav-item {
  height: auto;
  justify-content: flex-start;
}
.footer-icon {
  filter: invert(0.6);
}
.nav-item:hover {
  background: #121713;
  color: #f0f3ee;
}
.nav-item-active,
.nav-item-active:hover {
  background: var(--brand);
  color: var(--accent-foreground);
  font-weight: 700;
}
</style>
