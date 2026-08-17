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
        try {
          const me = await http.get('/me')
          sessionStorage.setItem('login_role', me.data.data.role)
          await router.push('/main')
        } catch (err: any) {
          sessionStorage.removeItem('login_token')
          errorMessage.value = err.response?.data?.message || '获取用户信息失败'
          loginErrorDialogVisible.value = true
        }
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

  <div class="login-page">
    <!-- 氛围光斑 -->
    <div class="orb orb-a" />
    <div class="orb orb-b" />

    <!-- 左侧品牌区 -->
    <section class="login-hero">
      <div class="brand-mark">PX</div>
      <h1 class="brand-title">Pixels 授权中心</h1>
      <p class="brand-sub">License &amp; Authorization Management</p>
      <ul class="brand-points">
        <li><span class="dot" />集中管理产品授权与设备配额</li>
        <li><span class="dot" />实时掌握授权状态与剩余时长</li>
        <li><span class="dot" />支持 CMS / GoPico 多产品线</li>
      </ul>
    </section>

    <!-- 右侧登录卡片 -->
    <section class="login-card">
      <h2 class="card-title">欢迎回来</h2>
      <p class="card-tip">请登录管理员账号</p>

      <el-input
        v-model="username"
        size="large"
        placeholder="请输入账号"
        :prefix-icon="Avatar"
        @keyup.enter="login"
      />

      <el-input
        v-model="password"
        class="pwd-input"
        :type="showPassword ? 'text' : 'password'"
        placeholder="请输入密码"
        size="large"
        :prefix-icon="Lock"
        clearable
        @keyup.enter="login"
      >
        <template #suffix>
          <el-button
            circle
            size="small"
            :icon="showPassword ? View : Hide"
            @click="togglePasswordVisibility"
          />
        </template>
      </el-input>

      <el-button
        class="login-btn"
        type="primary"
        size="large"
        :loading="isLoggingIn"
        :disabled="isLoggingIn"
        @click="login"
      >
        登 录
      </el-button>

      <p class="card-footer">Pixels · 安全授权服务</p>
    </section>
  </div>
</template>

<style scoped>
.login-page {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 96px;
  padding: 40px 24px;
  position: relative;
  overflow: hidden;
}

/* 氛围光斑 */
.orb {
  position: absolute;
  border-radius: 50%;
  filter: blur(90px);
  pointer-events: none;
}
.orb-a {
  width: 420px;
  height: 420px;
  left: -120px;
  top: -120px;
  background: radial-gradient(circle, rgba(34, 211, 238, 0.22), transparent 70%);
  animation: float 9s ease-in-out infinite;
}
.orb-b {
  width: 460px;
  height: 460px;
  right: -140px;
  bottom: -160px;
  background: radial-gradient(circle, rgba(139, 92, 246, 0.2), transparent 70%);
  animation: float 11s ease-in-out infinite reverse;
}
@keyframes float {
  0%, 100% { transform: translateY(0); }
  50% { transform: translateY(26px); }
}

/* 左侧品牌区 */
.login-hero {
  max-width: 460px;
}
.brand-mark {
  width: 64px;
  height: 64px;
  border-radius: 18px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 24px;
  font-weight: 800;
  letter-spacing: 0.04em;
  color: #04060e;
  background: var(--gd-gradient);
  box-shadow: 0 0 32px rgba(34, 211, 238, 0.45);
  margin-bottom: 28px;
}
.brand-title {
  margin: 0;
  font-size: 42px;
  font-weight: 800;
  letter-spacing: 0.02em;
  line-height: 1.2;
  background: linear-gradient(120deg, #e8eefb 20%, #67e8f9 55%, #a78bfa 90%);
  -webkit-background-clip: text;
  background-clip: text;
  color: transparent;
}
.brand-sub {
  margin: 12px 0 32px;
  font-size: 14px;
  letter-spacing: 0.22em;
  text-transform: uppercase;
  color: var(--gd-text-2);
}
.brand-points {
  list-style: none;
  margin: 0;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 14px;
  color: var(--gd-text-2);
  font-size: 15px;
}
.brand-points .dot {
  display: inline-block;
  width: 7px;
  height: 7px;
  border-radius: 50%;
  margin-right: 12px;
  background: var(--gd-cyan);
  box-shadow: 0 0 10px rgba(34, 211, 238, 0.8);
  vertical-align: 2px;
}

/* 右侧登录卡片 */
.login-card {
  width: 400px;
  padding: 40px 36px 28px;
  border-radius: 18px;
  background: var(--gd-glass);
  border: 1px solid var(--gd-line);
  backdrop-filter: blur(18px);
  -webkit-backdrop-filter: blur(18px);
  box-shadow: 0 24px 64px rgba(0, 0, 0, 0.45), 0 0 0 1px rgba(255, 255, 255, 0.03) inset;
  position: relative;
}
.login-card::before {
  content: "";
  position: absolute;
  top: 0;
  left: 24px;
  right: 24px;
  height: 2px;
  border-radius: 2px;
  background: var(--gd-gradient);
  opacity: 0.9;
}
.card-title {
  margin: 0;
  font-size: 26px;
  font-weight: 700;
  color: var(--gd-text-1);
}
.card-tip {
  margin: 8px 0 28px;
  font-size: 14px;
  color: var(--gd-text-2);
}
.pwd-input {
  margin: 16px 0 28px;
}
.login-btn {
  width: 100%;
  height: 46px;
  font-size: 16px;
}
.card-footer {
  margin: 26px 0 0;
  text-align: center;
  font-size: 12px;
  letter-spacing: 0.12em;
  color: var(--gd-text-2);
  opacity: 0.7;
}

/* 小屏适配：隐藏品牌区 */
@media (max-width: 900px) {
  .login-page {
    gap: 0;
  }
  .login-hero {
    display: none;
  }
  .login-card {
    width: 100%;
    max-width: 400px;
  }
}
</style>
