<script setup lang="ts">
import transIcon from '@/assets/ic_translate.svg'
import avatarIcon from '@/assets/ic_avatar.svg'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'

const i18n = useI18n()
const router = useRouter()

const handleTranslateClick = (command: string) => {
  console.log('translate-clicked', command)
  localStorage.setItem('language', command)
  i18n.locale.value = localStorage.getItem('language') || 'zh'
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

        <span class="!text-small font-semibold text-amber-600">{{props.authInfo}}</span>

        <div class="w-6"></div>

        <el-dropdown trigger="click" @command="handleTranslateClick">
          <el-image
            :src="transIcon"
            fit="cover"
            style="width: 20px; height: 20px; cursor: pointer"
          ></el-image>

          <template v-slot:dropdown>
            <el-dropdown-menu>
              <el-dropdown-item command="zh">简体中文</el-dropdown-item>
              <el-dropdown-item command="en">English</el-dropdown-item>
            </el-dropdown-menu>
          </template>
        </el-dropdown>
      </div>

      <div class="w-6"></div>

      <el-image
        :src="avatarIcon"
        fit="contain"
        style="width: 32px; height: 32px; cursor: pointer"
        @click="handleClickUser"
      ></el-image>

      <div class="w-6"></div>
    </div>
  </div>
</template>

<style scoped></style>
