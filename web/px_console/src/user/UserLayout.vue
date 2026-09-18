<script setup lang="ts">
import { computed, onMounted, onUnmounted } from "vue";
import { useRoute, useRouter } from "vue-router";
import { logoutUser } from "./api";
import { USER_SESSION_EXPIRED_EVENT } from "./http";
import { useI18n } from "vue-i18n";

const route = useRoute();
const router = useRouter();
const { t } = useI18n();
const selected = computed(() => [route.path]);

function sessionExpired() {
    void router.replace({ path: "/user/login", query: { redirect: route.fullPath } });
}

onMounted(() => window.addEventListener(USER_SESSION_EXPIRED_EVENT, sessionExpired));
onUnmounted(() => window.removeEventListener(USER_SESSION_EXPIRED_EVENT, sessionExpired));

function onMenuClick({ key }: { key: string | number }) {
    void router.push(String(key));
}

async function logout() {
    try {
        await logoutUser();
    } finally {
        await router.replace("/user/login");
    }
}
</script>

<template>
    <a-layout class="min-h-screen">
        <a-layout-sider width="210" theme="dark">
            <div class="px-6 py-7 text-xl font-semibold text-white">
                {{ t("userPortal.title") }}
            </div>
            <a-menu theme="dark" mode="inline" :selected-keys="selected" @click="onMenuClick">
                <a-menu-item key="/user/home">{{ t("userPortal.navigation.home") }}</a-menu-item>
                <a-menu-item key="/user/devices">{{
                    t("userPortal.navigation.devices")
                }}</a-menu-item>
                <a-menu-item key="/user/apps">{{ t("userPortal.navigation.apps") }}</a-menu-item>
                <a-menu-item key="/user/activity">{{
                    t("userPortal.navigation.activity")
                }}</a-menu-item>
                <a-menu-item key="/user/recordings">{{
                    t("userPortal.navigation.recordings")
                }}</a-menu-item>
                <a-menu-item key="/user/profile">{{
                    t("userPortal.navigation.profile")
                }}</a-menu-item>
            </a-menu>
        </a-layout-sider>
        <a-layout>
            <a-layout-header class="!flex !items-center !justify-between !bg-white !px-7">
                <span class="text-lg font-semibold">{{
                    t(String(route.meta.titleKey || "userPortal.title"))
                }}</span>
                <a-button @click="logout">{{ t("userPortal.logout") }}</a-button>
            </a-layout-header>
            <a-layout-content class="bg-slate-50 p-7"><RouterView /></a-layout-content>
        </a-layout>
    </a-layout>
</template>
