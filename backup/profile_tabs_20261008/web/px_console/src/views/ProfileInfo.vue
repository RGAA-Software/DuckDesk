<script setup lang="ts">
import { onMounted, reactive, ref } from "vue";
import { notification } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import { useRouter } from "vue-router";
import {
    changeAdminPassword,
    logoutAdmin,
    queryAdminSession,
    type AdminProfile,
} from "@/model/admin_session_api";

const { t } = useI18n();
const router = useRouter();
const profile = ref<AdminProfile>();
const saving = ref(false);
const form = reactive({ currentPassword: "", newPassword: "", confirmPassword: "" });

onMounted(async () => {
    profile.value = (await queryAdminSession()) || undefined;
});

async function changePassword() {
    if (!form.currentPassword || form.newPassword.length < 8 || form.newPassword.length > 128) {
        notification.error({ message: t("adminProfile.passwordInvalid") });
        return;
    }
    if (form.newPassword !== form.confirmPassword) {
        notification.error({ message: t("adminProfile.passwordMismatch") });
        return;
    }
    saving.value = true;
    try {
        await changeAdminPassword(form.currentPassword, form.newPassword);
        notification.success({ message: t("adminProfile.passwordChanged") });
        await logout();
    } catch {
        notification.error({ message: t("adminProfile.passwordFailed") });
    } finally {
        saving.value = false;
    }
}

async function logout() {
    await logoutAdmin();
    await router.replace("/");
}
</script>

<template>
    <a-space direction="vertical" size="large" class="w-full">
        <a-card :title="t('adminProfile.account')">
            <a-descriptions :column="1" bordered>
                <a-descriptions-item :label="t('adminProfile.username')">{{
                    profile?.username || "-"
                }}</a-descriptions-item>
                <a-descriptions-item :label="t('adminProfile.role')">{{
                    profile ? t(`identity.roles.${profile.role}`) : "-"
                }}</a-descriptions-item>
                <a-descriptions-item :label="t('adminProfile.createdAt')">{{
                    profile ? new Date(profile.created_at).toLocaleString() : "-"
                }}</a-descriptions-item>
            </a-descriptions>
        </a-card>

        <a-card :title="t('adminProfile.changePassword')">
            <a-alert
                type="info"
                show-icon
                :message="t('adminProfile.passwordNotice')"
                style="margin-bottom: 16px"
            />
            <a-form layout="vertical" style="max-width: 520px" @finish="changePassword">
                <a-form-item :label="t('adminProfile.currentPassword')"
                    ><a-input-password
                        v-model:value="form.currentPassword"
                        autocomplete="current-password"
                /></a-form-item>
                <a-form-item :label="t('adminProfile.newPassword')"
                    ><a-input-password v-model:value="form.newPassword" autocomplete="new-password"
                /></a-form-item>
                <a-form-item :label="t('adminProfile.confirmPassword')"
                    ><a-input-password
                        v-model:value="form.confirmPassword"
                        autocomplete="new-password"
                /></a-form-item>
                <a-button type="primary" html-type="submit" :loading="saving">{{
                    t("adminProfile.changePassword")
                }}</a-button>
            </a-form>
        </a-card>

        <a-card :title="t('adminProfile.session')">
            <a-button danger @click="logout">{{ t("adminProfile.logout") }}</a-button>
        </a-card>
    </a-space>
</template>
