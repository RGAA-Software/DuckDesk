<script setup lang="ts">
import { ref } from "vue";
import { notification } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import { useRouter } from "vue-router";
import iconLogo from "@/assets/ic_logo.png";
import { loginAdmin } from "@/model/admin_session_api";

const { t } = useI18n();
const router = useRouter();
const username = ref(localStorage.getItem("pixels.admin.username") || "");
const password = ref("");
const loading = ref(false);

async function login() {
    if (!username.value || !password.value || loading.value) return;
    loading.value = true;
    try {
        const profile = await loginAdmin(username.value, password.value);
        if (!profile) {
            notification.error({ message: t("adminLogin.failed") });
            return;
        }
        localStorage.setItem("pixels.admin.username", username.value);
        notification.success({ message: t("adminLogin.succeeded") });
        await router.replace("/home");
    } catch {
        notification.error({ message: t("adminLogin.failed") });
    } finally {
        loading.value = false;
    }
}
</script>

<template>
    <div class="min-h-screen flex items-center justify-center">
        <a-card :title="t('adminLogin.title')" style="width: 420px">
            <div class="flex justify-center" style="margin-bottom: 24px">
                <a-image :src="iconLogo" class="w-38" :preview="false" />
            </div>
            <a-alert
                type="info"
                show-icon
                :message="t('adminLogin.notice')"
                style="margin-bottom: 20px"
            />
            <a-form layout="vertical" @finish="login">
                <a-form-item :label="t('adminLogin.username')">
                    <a-input v-model:value="username" autocomplete="username" />
                </a-form-item>
                <a-form-item :label="t('adminLogin.password')">
                    <a-input-password v-model:value="password" autocomplete="current-password" />
                </a-form-item>
                <a-button
                    type="primary"
                    html-type="submit"
                    block
                    :loading="loading"
                    :disabled="!username || !password"
                >
                    {{ t("adminLogin.submit") }}
                </a-button>
            </a-form>
        </a-card>
    </div>
</template>
