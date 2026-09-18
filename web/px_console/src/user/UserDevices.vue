<script setup lang="ts">
import { onMounted, onUnmounted, ref } from "vue";
import { message } from "ant-design-vue";
import { getDevicesPage, openDevice, type DeviceSummary } from "./api";
const devices = ref<DeviceSummary[]>([]);
const loading = ref(false);
const page = ref(1);
const pageSize = 10;
const total = ref(0);
const keyword = ref("");
let refreshing = false;
let timer = 0;
async function refresh(showLoading = false) {
    if (refreshing) return;
    refreshing = true;
    if (showLoading) loading.value = true;
    try {
        const result = await getDevicesPage(page.value, pageSize, keyword.value);
        devices.value = result.items;
        total.value = result.total;
    } catch {
        if (showLoading) message.error("设备列表加载失败");
    } finally {
        refreshing = false;
        loading.value = false;
    }
}
async function connect(device: DeviceSummary) {
    try {
        await openDevice(device.device_id);
    } catch {
        message.error("无法获取连接信息，请确认设备节点在线且授权有效");
    }
}
function search() {
    page.value = 1;
    void refresh(true);
}
function changePage(value: number) {
    page.value = value;
    void refresh(true);
}
onMounted(() => {
    void refresh(true);
    timer = window.setInterval(() => void refresh(), 10000);
});
onUnmounted(() => window.clearInterval(timer));
</script>
<template>
    <a-card title="我的远程桌面">
        <template #extra
            ><a-space
                ><a-input-search
                    v-model:value="keyword"
                    allow-clear
                    placeholder="设备名称或 ID"
                    @search="search"
                /><a-button @click="refresh(true)" :loading="loading">刷新</a-button></a-space
            ></template
        >
        <a-empty
            v-if="!loading && devices.length === 0"
            description="管理员尚未向你或你的用户组授权设备"
        />
        <a-list
            v-else
            :data-source="devices"
            :loading="loading"
            :pagination="{
                current: page,
                pageSize,
                total,
                showSizeChanger: false,
                onChange: changePage,
            }"
        >
            <template #renderItem="{ item }"
                ><a-list-item>
                    <a-list-item-meta
                        :title="item.name || item.device_id"
                        :description="`${item.platform} · ${item.public_code}`"
                    />
                    <a-tag :color="item.disabled ? 'default' : 'blue'">{{
                        item.disabled ? "已停用" : "可用"
                    }}</a-tag>
                    <a-button
                        class="ml-4"
                        type="primary"
                        :disabled="item.disabled"
                        @click="connect(item)"
                        >连接</a-button
                    >
                </a-list-item></template
            >
        </a-list>
    </a-card>
</template>
