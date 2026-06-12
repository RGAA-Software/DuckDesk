<script setup lang="ts">
  import { Ticket, UserFilled } from '@element-plus/icons-vue'
  import router from '@/router'
  import {RouterView, useRoute} from 'vue-router'
  import {ref} from "vue";
  import http from '@/utils/http'

  import CreateAuthorizationDialog from "@/components/CreateAuthorizationDialog.vue";

  const route = useRoute()
  const handleOpen = (key: string, keyPath: string[]) => {
    console.log(key, keyPath)
  }
  const handleClose = (key: string, keyPath: string[]) => {
    console.log(key, keyPath)
  }

  const createDialogVisible = ref(false)

  const openDialog = () => {
    createDialogVisible.value = true
  }

  const logout = async () => {
    try {
      const response = await http.post('/log_out')
      console.log("the response: ", response.data)
    } catch (err: any) {
      console.log("err", err)
    } finally {
      sessionStorage.removeItem('login_token')
      await router.push('/')
      // loading.value = false
    }
  }

</script>

<template>
  <div class="w-screen h-screen">
    <el-container class="h-full">
      <el-header class="flex items-center gap-4">
        <el-text class="!text-lg font-bold" type="primary">授权管理系统</el-text>
        <el-button type="primary" @click="openDialog" >创建授权</el-button>
        <el-button @click="logout" >退出登录</el-button>
      </el-header>
      <el-container>
        <el-aside width="200px" class="">
          <el-menu
            class="no-border-menu"
            @open="handleOpen"
            @close="handleClose"
            :default-active="route.path"
            router
          >
            <el-menu-item index="/main/auth-list">
              <el-icon><Ticket /></el-icon>
              <span>授权列表</span>
            </el-menu-item>

<!--            <el-menu-item index="/main/admin-list">-->
<!--              <el-icon><UserFilled /></el-icon>-->
<!--              <span>管理员信息</span>-->
<!--            </el-menu-item>-->

          </el-menu>
        </el-aside>
        <el-main class="bg-gray-100 !p-0 !m-0">
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
</style>
