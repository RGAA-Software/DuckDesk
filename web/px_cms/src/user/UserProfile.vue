<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { message } from 'ant-design-vue'
import { changeUserPassword, queryUser, updateUserName, type UserProfile } from './api'
const profile = ref<UserProfile | null>(null)
const username = ref('')
const currentPassword = ref('')
const newPassword = ref('')
onMounted(async () => { profile.value = await queryUser(); username.value = profile.value?.username || '' })
async function saveName() { profile.value = await updateUserName(username.value); message.success('用户名已更新') }
async function savePassword() { try { profile.value = await changeUserPassword(currentPassword.value, newPassword.value); currentPassword.value=''; newPassword.value=''; message.success('密码已更新，其他会话已退出') } catch { message.error('当前密码错误或新密码不符合要求') } }
</script>
<template>
  <a-alert v-if="profile?.must_change_password" class="mb-5" type="warning" show-icon message="首次登录必须修改密码后才能访问资源" />
  <a-row :gutter="20">
    <a-col :span="12"><a-card title="个人资料"><a-form layout="vertical"><a-form-item label="用户名"><a-input v-model:value="username" /></a-form-item><a-form-item label="所属用户组"><a-space wrap><a-tag v-for="group in profile?.groups || []" :key="group.gid">{{ group.name }}</a-tag><span v-if="!profile?.groups?.length" class="text-gray-400">未加入用户组</span></a-space></a-form-item><a-button type="primary" :disabled="profile?.must_change_password" @click="saveName">保存</a-button></a-form></a-card></a-col>
    <a-col :span="12"><a-card title="修改密码"><a-form layout="vertical"><a-form-item label="当前密码"><a-input-password v-model:value="currentPassword" /></a-form-item><a-form-item label="新密码（8–128 位）"><a-input-password v-model:value="newPassword" /></a-form-item><a-button type="primary" @click="savePassword">修改密码</a-button></a-form></a-card></a-col>
  </a-row>
</template>
