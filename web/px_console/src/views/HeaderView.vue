<script setup lang="ts">
import transIcon from '@/assets/ic_translate.svg'
import avatarIcon from '@/assets/ic_avatar.svg'
import { BulbFilled, BulbOutlined } from '@ant-design/icons-vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'
import { useTheme } from '@/composables/useTheme'

const i18n = useI18n()
const router = useRouter()
const { isDark, toggleTheme } = useTheme()

const handleTranslateClick = (command: string) => {
  console.log('translate-clicked', command)
  localStorage.setItem('language', command)
  i18n.locale.value = localStorage.getItem('language') || 'zh'
}

const handleMenuClick = ({ key }: { key: string | number }) => {
  handleTranslateClick(key as string)
}

const handleClickUser = async () => {
  await router.push('/profile-info')
}

interface Props {
  title: string
  authInfo: string
}

const props = withDefaults(defineProps<Props>(), {
  title: '',
  authInfo: '默认授权',
})
</script>

<template>
  <div class="h-full p-2">
    <div class="h-full flex items-center">
      <span class="!text-xl w-100 font-bold text-slate-700">管理系统 - {{ props.title }}</span>

      <div class="flex justify-end w-full items-center">
        <span class="!text-small font-semibold text-amber-600">{{ props.authInfo }}</span>

        <div class="w-6"></div>

        <a-dropdown trigger="click">
          <img :src="transIcon" fit="cover" style="width: 20px; height: 20px; cursor: pointer" />
          <template #overlay>
            <a-menu @click="handleMenuClick">
              <a-menu-item key="zh">简体中文</a-menu-item>
              <a-menu-item key="en">English</a-menu-item>
            </a-menu>
          </template>
        </a-dropdown>

        <div class="w-6"></div>

        <a-button
          type="text"
          shape="circle"
          :title="isDark ? '切换亮色' : '切换暗色'"
          @click="toggleTheme"
        >
          <template #icon>
            <BulbFilled v-if="isDark" />
            <BulbOutlined v-else />
          </template>
        </a-button>
      </div>

      <div class="w-6"></div>

      <img
        :src="avatarIcon"
        style="width: 32px; height: 32px; cursor: pointer"
        @click="handleClickUser"
      />

      <div class="w-6"></div>
    </div>
  </div>
</template>

<style scoped></style>
