<script setup lang="ts">
  import { ref } from 'vue'
  import { Avatar, Lock, Hide, View, Warning} from '@element-plus/icons-vue'
  import { useRouter } from 'vue-router'
  import http from "@/utils/http";

  const router = useRouter()
  const username = ref('')
  const password = ref('')
  const showPassword = ref(false);
  const isLoggingIn = ref(false)

  const togglePasswordVisibility = () => {
    showPassword.value = !showPassword.value;
  };

  const kRespSuccessCode = 200;

  // 控制错误对话框显示
  const loginErrorDialogVisible = ref(false)
  // 错误信息
  const errorMessage = ref('')

  const login = async () => {
    if (isLoggingIn.value) return
    isLoggingIn.value = true
    try {
      const params = {
        author_name: username.value,
        author_token: password.value
      }
      const response = await http.post('/verify/author', params) // application/json
      if (kRespSuccessCode == response.data.code) {
        sessionStorage.setItem('login_token', response.data.data.token) //
        await router.push('/main')
        return
      }

      errorMessage.value = response.data.message || '用户名或密码错误'
      loginErrorDialogVisible.value = true
    } catch (err: any) {
      errorMessage.value = err.response?.data?.message || '用户名或密码错误'
      loginErrorDialogVisible.value = true
    } finally {
      isLoggingIn.value = false
    }
  }

</script>

<template>
  <!-- 错误提示对话框 -->
  <el-dialog
      v-model="loginErrorDialogVisible"
      title="登录失败"
      width="400"
  >
    <div class="error-message">
      <el-icon color="#F56C6C" size="20">
        <Warning />
      </el-icon>
      <span style="margin-left: 10px;">{{ errorMessage }}</span>
    </div>

    <template #footer>
      <div class="dialog-footer">
        <el-button type="primary" @click="loginErrorDialogVisible = false">
          确定
        </el-button>
      </div>
    </template>
  </el-dialog>

  <el-row class="h-screen flex justify-center items-center" :gutter="0">
    <el-col :span="4"></el-col>
    <!-- 左列 -->
    <el-col :span="6">
<!--      <el-card class="w-120 h-120">-->
<!--  -->
<!--      </el-card>-->
      <img class="w-130"
        src="@/assets/entry.png"

      />
    </el-col>
    <el-col :span="2"></el-col>

    <el-col :span="2"></el-col>
    <!-- 右列 -->
    <el-col :span="6" >
      <el-card style="max-width: 380px">
        <template #header>请登录管理员账号</template>

        <div>
          <el-input
            v-model="username"
            size="large"
            placeholder="请输入账号"
            :prefix-icon="Avatar"
          />
        </div>

        <div style="margin: 15px 0" />

        <el-input
          v-model="password"
          :type="showPassword ? 'text' : 'password'"
          placeholder="请输入密码"
          size="large"
          :prefix-icon="Lock"
          clearable
        >
          <template #suffix>
            <el-button
              circle
              size="small"
              :icon= "showPassword ? View : Hide"
              @click="togglePasswordVisibility"
            ></el-button>
          </template>
        </el-input>

        <div style="margin: 15px 0" />

        <el-button
          class="w-full !h-10"
          type="primary"
          :loading="isLoggingIn"
          :disabled="isLoggingIn"
          @click="login"
        >
          登录
        </el-button>
      </el-card>
    </el-col>

    <el-col :span="4"></el-col>

  </el-row>
</template>
