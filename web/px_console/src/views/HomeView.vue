<script setup lang="ts">
import AsideView from "@/views/AsideView.vue";
import HeaderView from "@/views/HeaderView.vue";
import { useRoute } from "vue-router";
import { computed } from "vue";
import { useI18n } from "vue-i18n";
import { useTheme } from "@/composables/useTheme";
import { useManagementEventConnection } from "@/model/management_events.ts";
const route = useRoute();
const { isDark } = useTheme();
const { t, locale } = useI18n();
useManagementEventConnection();

const sidebarWidth = computed(() => locale.value === "en" ? 240 : 160);

const headerTitle = computed(() => {
    const titleKey = route.meta.titleKey as string | undefined;
    return titleKey ? t(titleKey) : "";
});
</script>

<template>
    <a-layout class="console-layout">
        <a-layout-sider :width="sidebarWidth" :theme="isDark ? 'dark' : 'light'">
            <AsideView />
        </a-layout-sider>
        <a-layout class="console-main">
            <a-layout-header
                :style="{
                    background: isDark ? '#141414' : '#fff',
                    padding: 0,
                    height: 'auto',
                    lineHeight: 'normal',
                }"
            >
                <HeaderView :title="headerTitle" authInfo="" />
            </a-layout-header>
            <a-layout-content class="console-content">
                <RouterView />
            </a-layout-content>
        </a-layout>
    </a-layout>
</template>

<style scoped>
.console-layout {
    min-height: 100vh;
}
.console-main {
    min-width: 0;
}
.console-content {
    min-width: 0;
    padding: 16px;
}
</style>
