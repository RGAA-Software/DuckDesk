<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { message } from "ant-design-vue";
import { useRouter } from "vue-router";
import {
    changeUserPassword,
    queryUser,
    updateUserName,
    uploadUserAvatar,
    type UserProfile,
} from "./api";

const router = useRouter();
const profile = ref<UserProfile | null>(null);
const username = ref("");
const currentPassword = ref("");
const newPassword = ref("");
const avatarUploading = ref(false);
const avatarUrl = computed(() => profile.value?.avatar_url || "");

onMounted(async () => {
    profile.value = await queryUser();
    username.value = profile.value?.username || "";
});

async function saveName() {
    try {
        if (!profile.value) return;
        profile.value = await updateUserName(username.value, profile.value.revision);
        message.success("用户名已更新");
    } catch {
        message.error("用户名更新失败");
    }
}

async function savePassword() {
    try {
        await changeUserPassword(currentPassword.value, newPassword.value);
        message.success("密码已更新，请重新登录");
        await router.replace("/user/login");
    } catch {
        message.error("当前密码错误或新密码不符合要求");
    }
}

async function uploadAvatarFile(file: File) {
    if (file.size > 2 * 1024 * 1024) {
        message.error("头像不能超过 2 MB");
        return;
    }
    avatarUploading.value = true;
    try {
        if (!profile.value) return;
        profile.value = await uploadUserAvatar(file, profile.value.revision);
        message.success("头像已更新");
    } catch {
        message.error("头像上传失败，请使用 PNG、JPG 或 WebP 图片");
    } finally {
        avatarUploading.value = false;
    }
}

function beforeAvatarUpload(file: File) {
    void uploadAvatarFile(file);
    return false;
}
</script>
<template>
    <a-row :gutter="20">
        <a-col :xs="24" :xl="12">
            <a-card title="个人资料" class="mb-5">
                <div class="mb-5 flex items-center gap-4">
                    <a-avatar :size="72" :src="avatarUrl || undefined">{{
                        profile?.username?.slice(0, 1)
                    }}</a-avatar>
                    <a-upload
                        :show-upload-list="false"
                        accept="image/png,image/jpeg,image/webp"
                        :before-upload="beforeAvatarUpload"
                    >
                        <a-button :loading="avatarUploading">更换头像</a-button>
                    </a-upload>
                    <span class="text-gray-400">PNG、JPG 或 WebP，最大 2 MB</span>
                </div>
                <a-form layout="vertical">
                    <a-form-item label="用户名"><a-input v-model:value="username" /></a-form-item>
                    <a-form-item label="账号角色"
                        ><a-tag>{{ profile?.role || "user" }}</a-tag></a-form-item
                    >
                    <a-button type="primary" @click="saveName">保存</a-button>
                </a-form>
            </a-card>
        </a-col>
        <a-col :xs="24" :xl="12">
            <a-card title="修改密码" class="mb-5">
                <a-form layout="vertical">
                    <a-form-item label="当前密码"
                        ><a-input-password v-model:value="currentPassword"
                    /></a-form-item>
                    <a-form-item label="新密码（8–128 位）"
                        ><a-input-password v-model:value="newPassword"
                    /></a-form-item>
                    <a-button type="primary" @click="savePassword">修改密码</a-button>
                </a-form>
            </a-card>
        </a-col>
    </a-row>
</template>
