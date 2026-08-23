<script setup lang="ts">
import iconLogo from '@/assets/ic_logo.png'

import {
  ApiOutlined,
  AppstoreOutlined,
  DesktopOutlined,
  VideoCameraOutlined,
  HomeOutlined,
  LayoutOutlined,
  LineChartOutlined,
  LockOutlined,
  MessageOutlined,
  TeamOutlined,
  UserOutlined,
  CloudServerOutlined,
} from '@ant-design/icons-vue'
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import { useRouter } from 'vue-router'
import { useTheme } from '@/composables/useTheme'

const router = useRouter()
const route = useRoute()
const { isDark } = useTheme()

// 计算属性，自动获取当前路由路径
const activeMenu = computed(() => {
  const path = route.path

  const menuPaths = [
    '/resources',
    '/devices-list',
    '/online-connection',
    '/connection-monitor',
    '/video-wall',
    '/live-viewer',
    '/apps',
    '/security-internal',
    '/events',
    '/user-manager',
    '/group-manager',
    '/rtc-turn-settings',
    '/profile-info',
  ]

  if (menuPaths.includes(path)) {
    return path
  }

  return '/resources'
})

const handleMenuClick = ({ key }: { key: string | number }) => {
  router.push(key as string)
}

const handleClickLogo = async () => {
  await router.push('/resources')
}
</script>

<template>
  <div class="h-full">
    <div class="h-8"></div>
    <div class="flex justify-center">
      <img :src="iconLogo" class="w-38 cursor-pointer" @click="handleClickLogo" />
    </div>

    <div class="h-8"></div>
    <a-menu
      mode="inline"
      :theme="isDark ? 'dark' : 'light'"
      :selected-keys="[activeMenu]"
      class="!border-r-0"
      @click="handleMenuClick"
    >
      <a-menu-item key="/resources">
        <template #icon><HomeOutlined /></template>
        <span class="">资源总览</span>
      </a-menu-item>

      <a-menu-item key="/devices-list">
        <template #icon><DesktopOutlined /></template>
        <span class="">设备列表</span>
      </a-menu-item>

      <a-menu-item key="/online-connection">
        <template #icon><ApiOutlined /></template>
        <span class="">在线连接</span>
      </a-menu-item>

      <a-menu-item key="/connection-monitor">
        <template #icon><LineChartOutlined /></template>
        <span class="">连接监控</span>
      </a-menu-item>

      <a-menu-item key="/video-wall">
        <template #icon><LayoutOutlined /></template>
        <span class="">设备监控</span>
      </a-menu-item>

      <a-menu-item key="/live-viewer">
        <template #icon><VideoCameraOutlined /></template>
        <span class="">直播观看</span>
      </a-menu-item>

      <a-menu-item key="/apps">
        <template #icon><AppstoreOutlined /></template>
        <span class="">应用调度</span>
      </a-menu-item>

      <a-menu-item key="/security-internal">
        <template #icon><LockOutlined /></template>
        <span class="">安全审计</span>
      </a-menu-item>

      <a-menu-item key="/events">
        <template #icon><MessageOutlined /></template>
        <span class="">上报事件</span>
      </a-menu-item>

      <a-menu-item key="/user-manager">
        <template #icon><TeamOutlined /></template>
        <span class="">用户管理</span>
      </a-menu-item>

      <a-menu-item key="/group-manager">
        <template #icon><TeamOutlined /></template>
        <span class="">用户组管理</span>
      </a-menu-item>

      <a-menu-item key="/profile-info">
        <template #icon><UserOutlined /></template>
        <span class="">个人中心</span>
      </a-menu-item>

      <a-menu-item key="/rtc-turn-settings">
        <template #icon><CloudServerOutlined /></template>
        <span class="">WebRTC / TURN</span>
      </a-menu-item>
    </a-menu>
  </div>
</template>

<style scoped>
:deep(.ant-menu-item-selected) {
  font-weight: 600;
}
</style>
