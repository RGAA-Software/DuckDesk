<script setup lang="ts">
import AsideView from "@/views/AsideView.vue";
import HeaderView from "@/views/HeaderView.vue";
import { useRoute } from "vue-router";
import { computed } from "vue";
import { useTheme } from "@/composables/useTheme";
const route = useRoute();
const { isDark } = useTheme();

const headerTitle = computed(() => {
    return (route.meta.title as string) ?? "";
});
</script>

<template>
    <a-layout class="min-h-screen">
        <a-layout-sider width="160px" :theme="isDark ? 'dark' : 'light'">
            <AsideView />
        </a-layout-sider>
        <a-layout>
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
            <a-layout-content>
                <RouterView />
            </a-layout-content>
        </a-layout>
    </a-layout>
</template>

<style scoped></style>
