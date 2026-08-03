<script setup lang="ts">
import iconLogo from '@/assets/ic_logo.png'

import { Lock, MessageBox } from '@element-plus/icons-vue'
import { IpGridNine } from 'vue-icons-plus/ip'
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'

const i18n = useI18n()
const router = useRouter()

const handleOpen = (key: string, keyPath: string[]) => {
  console.log('open: ', key, keyPath)
}

const handleClose = (key: string, keyPath: string[]) => {
  console.log('close: ', key, keyPath)
}

const handleSelect = (key: string, keyPath: string[]) => {
  console.log('select', key, keyPath)
}

const route = useRoute()

// 计算属性，自动获取当前路由路径
const activeMenu = computed(() => {
  const path = route.path

  // 定义菜单路径映射
  const menuPaths = [
    '/resources',
    '/devices-list',
    'online-connection',
    '/connection-monitor',
    '/video-wall',
    '/security-internal',
    '/events',
    '/user-manager',
    '/profile-info',
  ]

  // 如果当前路径在菜单中，则选中对应菜单
  if (menuPaths.includes(path)) {
    return path
  }

  // 默认选中第一个
  return '/resources'
})

const handleClickLogo = async () => {
  await router.push('/resources')
}
</script>

<template>
  <div class="h-full">
    <div class="h-8"></div>
    <div class="flex justify-center">
      <el-image :src="iconLogo" class="w-38 cursor-pointer" @click="handleClickLogo" />
    </div>

    <div class="h-8"></div>
    <el-menu
      router
      :default-active="activeMenu"
      class="!border-r-0 !bg-blue-50"
      @open="handleOpen"
      @close="handleClose"
      @select="handleSelect"
    >
      <el-menu-item index="/resources">
        <el-icon><House /></el-icon>
        <span class="">资源总览</span>
      </el-menu-item>

      <el-menu-item index="/devices-list">
        <el-icon><Coin /></el-icon>
        <span class="">设备列表</span>
      </el-menu-item>

      <el-menu-item index="/online-connection">
        <el-icon><connection /></el-icon>
        <span class="">在线连接</span>
      </el-menu-item>

      <el-menu-item index="/connection-monitor">
        <el-icon><Monitor /></el-icon>
        <span class="">连接监控</span>
      </el-menu-item>

      <el-menu-item index="/video-wall">
        <el-icon><IpGridNine /></el-icon>
        <span class="">多画面墙</span>
      </el-menu-item>

      <el-menu-item index="/security-internal">
        <el-icon><Lock /></el-icon>
        <span class="">安全审计</span>
      </el-menu-item>

      <el-menu-item index="/events">
        <el-icon><MessageBox /></el-icon>
        <span class="">上报事件</span>
      </el-menu-item>

      <el-menu-item index="/user-manager">
        <el-icon><Notebook /></el-icon>
        <span class="">用户管理</span>
      </el-menu-item>

      <el-menu-item index="/profile-info">
        <el-icon><User /></el-icon>
        <span class="">个人中心</span>
      </el-menu-item>
    </el-menu>
  </div>
</template>

<style scoped>
.el-menu-item.is-active {
  font-weight: 600; /* 或 bold / 700 */
}
</style>
