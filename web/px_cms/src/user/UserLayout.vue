<script setup lang="ts">
import { computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { logoutUser } from './api'

const route = useRoute()
const router = useRouter()
const selected = computed(() => [route.path])

function onMenuClick({ key }: { key: string | number }) {
  void router.push(String(key))
}

async function logout() {
  try {
    await logoutUser()
  } finally {
    await router.replace('/user/login')
  }
}
</script>

<template>
  <a-layout class="min-h-screen">
    <a-layout-sider width="210" theme="dark">
      <div class="px-6 py-7 text-xl font-semibold text-white">Pixels 用户门户</div>
      <a-menu theme="dark" mode="inline" :selected-keys="selected" @click="onMenuClick">
        <a-menu-item key="/user/home">我的资源</a-menu-item>
        <a-menu-item key="/user/devices">我的远程桌面</a-menu-item>
        <a-menu-item key="/user/apps">云端应用</a-menu-item>
        <a-menu-item key="/user/activity">实例与活动</a-menu-item>
        <a-menu-item key="/user/profile">个人中心</a-menu-item>
      </a-menu>
    </a-layout-sider>
    <a-layout>
      <a-layout-header class="!flex !items-center !justify-between !bg-white !px-7">
        <span class="text-lg font-semibold">{{ route.meta.title }}</span>
        <a-button @click="logout">退出登录</a-button>
      </a-layout-header>
      <a-layout-content class="bg-slate-50 p-7"><RouterView /></a-layout-content>
    </a-layout>
  </a-layout>
</template>
