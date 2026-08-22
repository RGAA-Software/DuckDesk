<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { message, Modal } from 'ant-design-vue'
import { useRouter } from 'vue-router'
import {
  changeUserPassword,
  logoutAllUserSessions,
  queryUser,
  updateUserName,
  uploadUserAvatar,
  type UserProfile,
} from './api'

const router = useRouter()
const profile = ref<UserProfile | null>(null)
const username = ref('')
const currentPassword = ref('')
const newPassword = ref('')
const logoutPassword = ref('')
const avatarUploading = ref(false)
const avatarUrl = computed(() => profile.value?.avatar_path || '')

onMounted(async () => {
  profile.value = await queryUser()
  username.value = profile.value?.username || ''
})

async function saveName() {
  try {
    profile.value = await updateUserName(username.value)
    message.success('用户名已更新')
  } catch {
    message.error('用户名更新失败')
  }
}

async function savePassword() {
  try {
    profile.value = await changeUserPassword(currentPassword.value, newPassword.value)
    currentPassword.value = ''
    newPassword.value = ''
    message.success('密码已更新，其他会话已退出')
  } catch {
    message.error('当前密码错误或新密码不符合要求')
  }
}

async function uploadAvatarFile(file: File) {
  if (file.size > 2 * 1024 * 1024) {
    message.error('头像不能超过 2 MB')
    return
  }
  avatarUploading.value = true
  try {
    profile.value = await uploadUserAvatar(file)
    message.success('头像已更新')
  } catch {
    message.error('头像上传失败，请使用 PNG、JPG 或 WebP 图片')
  } finally {
    avatarUploading.value = false
  }
}

function beforeAvatarUpload(file: File) {
  void uploadAvatarFile(file)
  return false
}

function confirmLogoutAll() {
  if (!logoutPassword.value) {
    message.warning('请输入当前密码')
    return
  }
  Modal.confirm({
    title: '退出全部设备？',
    content: '确认后，本账号在 Panel 和网页端的全部会话都会立即失效。',
    okText: '全部退出',
    okType: 'danger',
    cancelText: '取消',
    async onOk() {
      try {
        await logoutAllUserSessions(logoutPassword.value)
        await router.replace('/user/login')
      } catch {
        message.error('当前密码错误，未退出任何会话')
        throw new Error('logout-all failed')
      }
    },
  })
}
</script>
<template>
  <a-row :gutter="20">
    <a-col :xs="24" :xl="12">
      <a-card title="个人资料" class="mb-5">
        <div class="mb-5 flex items-center gap-4">
          <a-avatar :size="72" :src="avatarUrl || undefined">{{ profile?.username?.slice(0, 1) }}</a-avatar>
          <a-upload :show-upload-list="false" accept="image/png,image/jpeg,image/webp" :before-upload="beforeAvatarUpload">
            <a-button :loading="avatarUploading">更换头像</a-button>
          </a-upload>
          <span class="text-gray-400">PNG、JPG 或 WebP，最大 2 MB</span>
        </div>
        <a-form layout="vertical">
          <a-form-item label="用户名"><a-input v-model:value="username" /></a-form-item>
          <a-form-item label="所属用户组">
            <a-space wrap>
              <a-tag v-for="group in profile?.groups || []" :key="group.gid">{{ group.name }}</a-tag>
              <span v-if="!profile?.groups?.length" class="text-gray-400">未加入用户组</span>
            </a-space>
          </a-form-item>
          <a-button type="primary" @click="saveName">保存</a-button>
        </a-form>
      </a-card>
    </a-col>
    <a-col :xs="24" :xl="12">
      <a-card title="修改密码" class="mb-5">
        <a-form layout="vertical">
          <a-form-item label="当前密码"><a-input-password v-model:value="currentPassword" /></a-form-item>
          <a-form-item label="新密码（8–128 位）"><a-input-password v-model:value="newPassword" /></a-form-item>
          <a-button type="primary" @click="savePassword">修改密码</a-button>
        </a-form>
      </a-card>
      <a-card title="会话安全">
        <p class="mb-4 text-gray-500">发现异常登录时，可让当前账号在所有 Panel 和网页端立即退出。</p>
        <a-input-password v-model:value="logoutPassword" class="mb-4" placeholder="请输入当前密码确认" />
        <a-button danger @click="confirmLogoutAll">退出全部设备</a-button>
      </a-card>
    </a-col>
  </a-row>
</template>
