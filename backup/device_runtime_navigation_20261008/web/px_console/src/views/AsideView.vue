<script setup lang="ts">
import PixelsBrand from "@/components/PixelsBrand.vue";

import {
    ApiOutlined,
    AppstoreOutlined,
    BellOutlined,
    DesktopOutlined,
    HomeOutlined,
    LockOutlined,
    TeamOutlined,
    UserOutlined,
} from "@ant-design/icons-vue";
import { computed } from "vue";
import { useI18n } from "vue-i18n";
import { useRoute } from "vue-router";
import { useRouter } from "vue-router";
import { useTheme } from "@/composables/useTheme";

const router = useRouter();
const route = useRoute();
const { isDark } = useTheme();
const { t } = useI18n();

// 计算属性，自动获取当前路由路径
const activeMenu = computed(() => {
    const path = route.path;

    const menuPaths = [
        "/resources",
        "/devices-list",
        "/online-connection",
        "/apps",
        "/security-internal",
        "/telemetry-alerts",
        "/user-manager",
        "/group-manager",
        "/profile-info",
    ];

    if (menuPaths.includes(path)) {
        return path;
    }

    return "/resources";
});

const handleMenuClick = ({ key }: { key: string | number }) => {
    router.push(key as string);
};

const handleClickLogo = async () => {
    await router.push("/resources");
};
</script>

<template>
    <div class="h-full">
        <div class="h-8"></div>
        <div class="flex justify-center">
            <PixelsBrand class="cursor-pointer text-2xl" @click="handleClickLogo" />
        </div>

        <div class="h-8"></div>
        <a-menu
            mode="inline"
            :theme="isDark ? 'dark' : 'light'"
            :selected-keys="[activeMenu]"
            class="!border-r-0"
            @click="handleMenuClick"
        >
            <a-menu-item key="/resources">
                <template #icon><HomeOutlined /></template>
                <span>{{ t("navigation.dashboard") }}</span>
            </a-menu-item>

            <a-menu-item key="/devices-list">
                <template #icon><DesktopOutlined /></template>
                <span>{{ t("navigation.devices") }}</span>
            </a-menu-item>

            <a-menu-item key="/online-connection">
                <template #icon><ApiOutlined /></template>
                <span>{{ t("navigation.online") }}</span>
            </a-menu-item>

            <a-menu-item key="/apps">
                <template #icon><AppstoreOutlined /></template>
                <span>{{ t("navigation.applications") }}</span>
            </a-menu-item>

            <a-menu-item key="/security-internal">
                <template #icon><LockOutlined /></template>
                <span>{{ t("navigation.security") }}</span>
            </a-menu-item>

            <a-menu-item key="/telemetry-alerts">
                <template #icon><BellOutlined /></template>
                <span>{{ t("navigation.telemetryAlerts") }}</span>
            </a-menu-item>

            <a-menu-item key="/user-manager">
                <template #icon><TeamOutlined /></template>
                <span>{{ t("navigation.users") }}</span>
            </a-menu-item>

            <a-menu-item key="/group-manager">
                <template #icon><TeamOutlined /></template>
                <span>{{ t("navigation.groups") }}</span>
            </a-menu-item>

            <a-menu-item key="/profile-info">
                <template #icon><UserOutlined /></template>
                <span>{{ t("navigation.profile") }}</span>
            </a-menu-item>
        </a-menu>
    </div>
</template>

<style scoped>
:deep(.ant-menu-item-selected) {
    font-weight: 600;
}
</style>
