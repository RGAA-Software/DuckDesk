<script setup lang="ts">
  import { Monitor, Ticket, UserFilled } from '@element-plus/icons-vue'
  import router from '@/router'
  import {RouterView, useRoute} from 'vue-router'
  import {computed, onMounted, ref} from "vue";
  import http from '@/utils/http'

  import CreateAuthorizationDialog from "@/components/CreateAuthorizationDialog.vue";

  const route = useRoute()
  const currentRole = ref('')
  const isAdmin = computed(() => currentRole.value === 'admin')

  const createDialogVisible = ref(false)
  const isLoggingOut = ref(false)

  const openDialog = () => {
    createDialogVisible.value = true
  }

  const logout = async () => {
    if (isLoggingOut.value) return
    isLoggingOut.value = true
    try {
      await http.post('/log_out')
    } catch {
      // 本地退出仍然要清理 token，避免服务端异常时卡在登录态。
    } finally {
      sessionStorage.removeItem('login_token')
      sessionStorage.removeItem('login_role')
      await router.push('/')
      isLoggingOut.value = false
    }
  }

  onMounted(async () => {
    const response = await http.get('/me')
    currentRole.value = response.data.data.role
  })

</script>

<template>
  <div class="w-screen h-screen">
    <el-container class="h-full">
      <el-header class="app-header flex items-center gap-3">
        <span class="logo-dot" />
        <span class="app-title">授权管理系统</span>
        <span class="app-badge">GoDesk</span>
        <div class="flex-1" />
        <el-button v-if="isAdmin" type="primary" @click="openDialog">创建授权</el-button>
        <el-button :loading="isLoggingOut" :disabled="isLoggingOut" @click="logout">退出登录</el-button>
      </el-header>
      <el-container class="app-body">
        <el-aside width="216px" class="app-aside">
          <el-menu
            class="no-border-menu"
            :default-active="route.path"
            router
          >
            <el-menu-item index="/main/auth-list">
              <el-icon><Ticket /></el-icon>
              <span>授权列表</span>
            </el-menu-item>

            <el-menu-item index="/main/gopico-auth-list">
              <el-icon><Monitor /></el-icon>
              <span>GoPico 授权</span>
            </el-menu-item>

            <el-menu-item v-if="isAdmin" index="/main/admin-list">
              <el-icon><UserFilled /></el-icon>
              <span>管理员信息</span>
            </el-menu-item>

          </el-menu>
        </el-aside>
        <el-main class="app-main">
          <RouterView/>
        </el-main>
      </el-container>
    </el-container>

    <CreateAuthorizationDialog  v-model="createDialogVisible" />
  </div>
</template>

<style scoped>
.no-border-menu {
  border-right: none !important;
}

/* 顶栏 */
.app-header {
  height: 60px;
  background: rgba(8, 12, 24, 0.72);
  backdrop-filter: blur(14px);
  -webkit-backdrop-filter: blur(14px);
  border-bottom: 1px solid var(--gd-line);
}
.logo-dot {
  width: 28px;
  height: 28px;
  border-radius: 8px;
  background: var(--gd-gradient);
  box-shadow: 0 0 16px rgba(34, 211, 238, 0.5);
  flex: none;
}
.app-title {
  font-size: 17px;
  font-weight: 700;
  letter-spacing: 0.02em;
  background: linear-gradient(120deg, #e8eefb 30%, #67e8f9 80%);
  -webkit-background-clip: text;
  background-clip: text;
  color: transparent;
}
.app-badge {
  font-size: 11px;
  letter-spacing: 0.14em;
  padding: 3px 10px;
  border-radius: 999px;
  color: var(--gd-cyan);
  border: 1px solid rgba(34, 211, 238, 0.35);
  background: rgba(34, 211, 238, 0.08);
}

/* 侧边栏 */
.app-body {
  height: calc(100% - 60px);
}
.app-aside {
  background: rgba(8, 12, 24, 0.55);
  border-right: 1px solid var(--gd-line);
  padding: 14px 10px;
}
.app-aside :deep(.el-menu-item) {
  border-radius: 10px;
  margin: 4px 0;
  height: 44px;
  color: var(--gd-text-2);
  transition: background 0.2s ease, color 0.2s ease;
}
.app-aside :deep(.el-menu-item:hover) {
  background: rgba(34, 211, 238, 0.08);
  color: var(--gd-cyan);
}
.app-aside :deep(.el-menu-item.is-active) {
  background: linear-gradient(90deg, rgba(34, 211, 238, 0.16), rgba(139, 92, 246, 0.1));
  color: #67e8f9;
  font-weight: 600;
  box-shadow: inset 2px 0 0 var(--gd-cyan);
}

/* 内容区 */
.app-main {
  background: transparent;
  padding: 20px 24px;
  overflow: auto;
}
</style>
