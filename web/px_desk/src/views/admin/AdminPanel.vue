<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useI18n } from "vue-i18n";
import { useRouter } from "vue-router";
import adminHttp, { clearAdminToken, getAdminToken } from "@/adminHttp.ts";
import iconLogo from "@/assets/icon/ic_trans_icon_blue.png";

const { t } = useI18n();
const router = useRouter();

// ---------- 类型 ----------
interface ConsultItem {
    id: string;
    title: string;
    your_name: string;
    consult_type: string;
    description: string;
    email: string;
    wechat: string;
    qq: string;
    created_at: string;
    processed: boolean;
    revision: number;
}

interface IssueItem {
    id: string;
    title: string;
    your_name: string;
    description: string;
    version: string;
    os: string;
    email: string;
    wechat: string;
    qq: string;
    created_at: string;
    processed: boolean;
    revision: number;
}

type TabKey = "consults" | "issues";
type FilterKey = "all" | "pending" | "done";

// ---------- 状态 ----------
const activeTab = ref<TabKey>("consults");
const activeFilter = ref<FilterKey>("all");
const page = ref(1);
const PAGE_SIZE = 10;
const loading = ref(false);
const loadError = ref(false);
const consults = ref<ConsultItem[]>([]);
const issues = ref<IssueItem[]>([]);
const markingId = ref("");
const expandedId = ref("");

const tabs = computed(() => [
    { key: "consults" as const, label: t("admin.consults"), accent: "#2f8fff" },
    { key: "issues" as const, label: t("admin.issues"), accent: "#2ac7c4" },
]);

const filters = computed(() => [
    { key: "all" as const, label: t("admin.filterAll") },
    { key: "pending" as const, label: t("admin.filterPending") },
    { key: "done" as const, label: t("admin.filterDone") },
]);

const currentList = computed(() =>
    activeTab.value === "consults" ? consults.value : issues.value,
);

// ---------- 数据加载 ----------
async function loadData() {
    loading.value = true;
    loadError.value = false;
    try {
        const params: Record<string, string | number | boolean> = {
            page: page.value,
            page_size: PAGE_SIZE,
        };
        if (activeFilter.value !== "all") {
            params.processed = activeFilter.value === "done";
        }
        const url = activeTab.value === "consults" ? "/api/desk/consults" : "/api/desk/issues";
        const { data } = await adminHttp.get(url, { params });
        if (!Array.isArray(data.items)) throw new Error("Invalid Desk response");
        const list = data.items;
        // 翻页翻过了头：回退一页重新加载
        if (list.length === 0 && page.value > 1) {
            page.value -= 1;
            loading.value = false;
            return loadData();
        }
        if (activeTab.value === "consults") {
            consults.value = list;
        }
 else {
            issues.value = list;
        }
    }
 catch {
        loadError.value = true;
    }
 finally {
        loading.value = false;
    }
}

function switchTab(key: TabKey) {
    if (loading.value) return;
    activeTab.value = key;
    page.value = 1;
    expandedId.value = "";
    loadData();
}

function switchFilter(key: FilterKey) {
    if (loading.value) return;
    activeFilter.value = key;
    page.value = 1;
    loadData();
}

function goPage(delta: number) {
    if (loading.value || page.value + delta < 1 || page.value + delta > 10000) return;
    page.value += delta;
    loadData();
}

async function toggleProcessed(item: ConsultItem | IssueItem) {
    if (markingId.value) return;
    markingId.value = item.id;
    try {
        await adminHttp.patch(`/api/desk/${activeTab.value}/${item.id}`, {
            expected_revision: item.revision,
            processed: !item.processed,
        });
        await loadData();
    }
 catch {
        loadError.value = true;
    }
 finally {
        markingId.value = "";
    }
}

async function logout() {
    try {
        await adminHttp.delete("/api/desk/admin/session");
        clearAdminToken();
        router.replace("/admin");
    }
 catch {
        loadError.value = true;
    }
}

onMounted(() => {
    if (!getAdminToken()) {
        router.replace("/admin");
        return;
    }
    loadData();
});
</script>

<template>
    <div class="min-h-screen">
        <!-- 顶栏 -->
        <header class="border-b border-cyber-line bg-cyber-nav">
            <div class="section-container flex h-14 items-center justify-between">
                <div class="flex items-center gap-2.5">
                    <img :src="iconLogo" alt="GoDesk" class="h-7 w-7" />
                    <span class="font-tech text-base font-bold tracking-[0.18em] text-cyber-text">
                        {{ t("admin.panelTitle") }}
                    </span>
                    <span class="cyber-dot ml-2"></span>
                </div>
                <div class="flex items-center gap-3">
                    <el-button class="!h-9" :disabled="loading" @click="loadData">{{
                        t("admin.refresh")
                    }}</el-button>
                    <el-button class="!h-9" @click="logout">{{ t("admin.logout") }}</el-button>
                </div>
            </div>
        </header>

        <div class="section-container py-8">
            <!-- 数据类型 + 状态筛选 -->
            <div class="flex flex-wrap items-center justify-between gap-4">
                <div class="flex gap-3">
                    <button
                        v-for="tab in tabs"
                        :key="tab.key"
                        :disabled="loading"
                        class="cyber-tab"
                        :class="{ 'cyber-tab-active': activeTab === tab.key }"
                        :style="{ '--ta': tab.accent }"
                        @click="switchTab(tab.key)"
                    >
                        {{ tab.label }}
                    </button>
                </div>

                <div class="flex gap-2">
                    <button
                        v-for="filter in filters"
                        :key="filter.key"
                        :disabled="loading"
                        class="cyber-tab !h-8 !px-4 !text-[11px]"
                        :class="{ 'cyber-tab-active': activeFilter === filter.key }"
                        @click="switchFilter(filter.key)"
                    >
                        {{ filter.label }}
                    </button>
                </div>
            </div>

            <!-- 表格 -->
            <div class="cyber-panel mt-6 overflow-x-auto">
                <!-- 咨询留言 -->
                <table v-if="activeTab === 'consults'" class="admin-table">
                    <thead>
                        <tr>
                            <th>{{ t("admin.time") }}</th>
                            <th>{{ t("admin.title") }}</th>
                            <th>{{ t("admin.name") }}</th>
                            <th>{{ t("admin.type") }}</th>
                            <th>{{ t("admin.content") }}</th>
                            <th>{{ t("admin.contact") }}</th>
                            <th>{{ t("admin.status") }}</th>
                            <th>{{ t("admin.action") }}</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr v-for="item in consults" :key="item.id">
                            <td class="whitespace-nowrap">
                                {{ new Date(item.created_at).toLocaleString() }}
                            </td>
                            <td>{{ item.title }}</td>
                            <td>{{ item.your_name }}</td>
                            <td>
                                <span
                                    class="admin-tag"
                                    :class="
                                        item.consult_type === 'enterprise' ? 'tag-cyan' : 'tag-blue'
                                    "
                                >
                                    {{
                                        item.consult_type === "enterprise"
                                            ? t("admin.typeEnterprise")
                                            : t("admin.typePersonal")
                                    }}
                                </span>
                            </td>
                            <td class="max-w-60">
                                <div class="truncate">{{ item.description }}</div>
                                <button
                                    class="expand-btn"
                                    @click="expandedId = expandedId === item.id ? '' : item.id"
                                >
                                    {{
                                        expandedId === item.id
                                            ? t("admin.collapse")
                                            : t("admin.expand")
                                    }}
                                </button>
                                <div
                                    v-if="expandedId === item.id"
                                    class="mt-2 whitespace-pre-wrap text-cyber-muted"
                                >
                                    {{ item.description }}
                                </div>
                            </td>
                            <td class="contact-cell">
                                <div v-if="item.email">{{ item.email }}</div>
                                <div v-if="item.wechat">WX: {{ item.wechat }}</div>
                                <div v-if="item.qq">QQ: {{ item.qq }}</div>
                            </td>
                            <td>
                                <span
                                    class="admin-tag"
                                    :class="item.processed ? 'tag-done' : 'tag-pending'"
                                >
                                    {{ item.processed ? t("admin.done") : t("admin.pending") }}
                                </span>
                            </td>
                            <td>
                                <button
                                    class="action-btn"
                                    :disabled="markingId === item.id"
                                    @click="toggleProcessed(item)"
                                >
                                    {{
                                        item.processed
                                            ? t("admin.markPending")
                                            : t("admin.markDone")
                                    }}
                                </button>
                            </td>
                        </tr>
                    </tbody>
                </table>

                <!-- 工单反馈 -->
                <table v-else class="admin-table">
                    <thead>
                        <tr>
                            <th>{{ t("admin.time") }}</th>
                            <th>{{ t("admin.issue") }}</th>
                            <th>{{ t("admin.name") }}</th>
                            <th>{{ t("admin.content") }}</th>
                            <th>{{ t("admin.versionOs") }}</th>
                            <th>{{ t("admin.contact") }}</th>
                            <th>{{ t("admin.status") }}</th>
                            <th>{{ t("admin.action") }}</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr v-for="item in issues" :key="item.id">
                            <td class="whitespace-nowrap">
                                {{ new Date(item.created_at).toLocaleString() }}
                            </td>
                            <td>{{ item.title }}</td>
                            <td>{{ item.your_name }}</td>
                            <td class="max-w-60">
                                <div class="truncate">{{ item.description }}</div>
                                <button
                                    class="expand-btn"
                                    @click="expandedId = expandedId === item.id ? '' : item.id"
                                >
                                    {{
                                        expandedId === item.id
                                            ? t("admin.collapse")
                                            : t("admin.expand")
                                    }}
                                </button>
                                <div
                                    v-if="expandedId === item.id"
                                    class="mt-2 whitespace-pre-wrap text-cyber-muted"
                                >
                                    {{ item.description }}
                                </div>
                            </td>
                            <td class="contact-cell">
                                <div v-if="item.version">v{{ item.version }}</div>
                                <div v-if="item.os">{{ item.os }}</div>
                            </td>
                            <td class="contact-cell">
                                <div v-if="item.email">{{ item.email }}</div>
                                <div v-if="item.wechat">WX: {{ item.wechat }}</div>
                                <div v-if="item.qq">QQ: {{ item.qq }}</div>
                            </td>
                            <td>
                                <span
                                    class="admin-tag"
                                    :class="item.processed ? 'tag-done' : 'tag-pending'"
                                >
                                    {{ item.processed ? t("admin.done") : t("admin.pending") }}
                                </span>
                            </td>
                            <td>
                                <button
                                    class="action-btn"
                                    :disabled="markingId === item.id"
                                    @click="toggleProcessed(item)"
                                >
                                    {{
                                        item.processed
                                            ? t("admin.markPending")
                                            : t("admin.markDone")
                                    }}
                                </button>
                            </td>
                        </tr>
                    </tbody>
                </table>

                <!-- 状态提示 -->
                <div
                    v-if="loading"
                    class="py-10 text-center font-tech text-sm tracking-wider text-cyber-muted"
                >
                    {{ t("admin.loading") }}
                </div>
                <div
                    v-else-if="loadError"
                    class="py-10 text-center font-tech text-sm tracking-wider text-cyber-red"
                >
                    {{ t("admin.loadFailed") }}
                </div>
                <div
                    v-else-if="currentList.length === 0"
                    class="py-10 text-center font-tech text-sm tracking-[0.2em] text-cyber-muted"
                >
                    {{ t("admin.noData") }}
                </div>
            </div>

            <!-- 分页 -->
            <div class="mt-6 flex items-center justify-center gap-4">
                <el-button class="!h-9" :disabled="page <= 1" @click="goPage(-1)">{{
                    t("admin.prev")
                }}</el-button>
                <span class="font-tech text-sm tracking-wider text-cyber-muted"
                    >{{ t("admin.page") }} {{ page }}</span
                >
                <el-button
                    class="!h-9"
                    :disabled="currentList.length < PAGE_SIZE"
                    @click="goPage(1)"
                >
                    {{ t("admin.next") }}
                </el-button>
            </div>
        </div>
    </div>
</template>

<style scoped>
.admin-table {
    width: 100%;
    font-size: 12px;
    border-collapse: collapse;
}
.admin-table th {
    height: 40px;
    padding: 0 12px;
    text-align: left;
    color: #7d887f;
    font-family: var(--font-tech);
    font-size: 11px;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    font-weight: 600;
    background: #0d100e;
    border-bottom: 1px solid #29332b;
    white-space: nowrap;
}
.admin-table td {
    padding: 12px;
    color: #d4d9d3;
    border-bottom: 1px solid #252e27;
    vertical-align: top;
}
.admin-table tbody tr:hover td {
    background: #141915;
}
.contact-cell {
    font-family: var(--font-tech);
    font-size: 11px;
    color: var(--muted);
    white-space: nowrap;
}
.admin-tag {
    display: inline-block;
    font-family: var(--font-tech);
    font-size: 10px;
    letter-spacing: 0.08em;
    padding: 2px 8px;
    border: 1px solid;
    white-space: nowrap;
}
.tag-blue {
    color: #6db3ff;
    border-color: #2b5c85;
}
.tag-cyan {
    color: #4dd9d4;
    border-color: #2b7a77;
}
.tag-done {
    color: #6db3ff;
    border-color: #2b5c85;
}
.tag-pending {
    color: #e6b960;
    border-color: #7b6231;
}
.action-btn {
    font-family: var(--font-tech);
    font-size: 11px;
    letter-spacing: 0.05em;
    padding: 4px 10px;
    color: #d7ddd7;
    border: 1px solid var(--frame);
    background: transparent;
    cursor: pointer;
    white-space: nowrap;
    transition:
        border-color 0.15s,
        color 0.15s;
}
.action-btn:hover:not(:disabled) {
    border-color: var(--brand);
    color: var(--brand);
}
.action-btn:disabled {
    opacity: 0.5;
    cursor: default;
}
.expand-btn {
    font-family: var(--font-tech);
    font-size: 10px;
    color: var(--brand);
    letter-spacing: 0.08em;
    cursor: pointer;
    background: transparent;
    border: 0;
    padding: 2px 0;
}
</style>
