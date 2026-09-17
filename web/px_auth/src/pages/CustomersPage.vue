<script setup lang="ts">
import { onMounted, ref } from "vue";
import { profile, request } from "../api";
import { t } from "../uiSettings";
import { useOperation } from "../useOperation";
import type { Customer } from "../licenseModel";
import RecordPager from "../components/RecordPager.vue";
const rows = ref<Customer[]>([]),
    name = ref(""),
    remark = ref(""),
    after = ref("");
const { busy, error, run } = useOperation();
async function load() {
    rows.value = await request<Customer[]>(
        "/customers?limit=50" + (after.value ? "&after=" + after.value : ""),
    );
}
async function create() {
    await request("/customers", "POST", { name: name.value, remark: remark.value });
    name.value = "";
    remark.value = "";
    await load();
}
function page(cursor: string) {
    after.value = cursor;
    void run(load);
}
onMounted(() => run(load));
</script>
<template>
    <section>
        <h2>{{ t("customers") }}</h2>
        <p v-if="error" role="alert" class="error">{{ t(error) }}</p>
        <RecordPager
            :busy="busy"
            :count="rows.length"
            @refresh="run(load)"
            @first="page('')"
            @next="page(rows[rows.length - 1]!.id)"
        />
        <form v-if="profile?.role === 'admin'" class="card" @submit.prevent="run(create)">
            <h3>{{ t("createCustomer") }}</h3>
            <label
                >{{ t("name") }}<input v-model="name" required maxlength="128" :disabled="busy"
            /></label>
            <label
                >{{ t("remark") }}<textarea v-model="remark" maxlength="1024" :disabled="busy" />
            </label>
            <div>
                <button class="primary" :disabled="busy">{{ t("create") }}</button>
            </div>
        </form>
        <div class="table-scroll">
            <table>
                <thead>
                    <tr>
                        <th>{{ t("name") }}</th>
                        <th>{{ t("remark") }}</th>
                        <th>{{ t("id") }}</th>
                    </tr>
                </thead>
                <tbody>
                    <tr v-for="row in rows" :key="row.id">
                        <td>{{ row.name }}</td>
                        <td>{{ row.remark }}</td>
                        <td class="mono">{{ row.id }}</td>
                    </tr>
                </tbody>
            </table>
        </div>
        <p v-if="!rows.length">{{ busy ? t("loading") : t("empty") }}</p>
    </section>
</template>
