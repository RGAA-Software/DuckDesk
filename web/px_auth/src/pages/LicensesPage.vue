<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { profile, request } from "../api";
import { readPayload, type License } from "../licenseModel";
import { t } from "../uiSettings";
import { useOperation } from "../useOperation";
import RecordPager from "../components/RecordPager.vue";
import LicenseForm from "../components/LicenseForm.vue";
const rows = ref<License[]>([]),
    after = ref(""),
    editing = ref(false),
    renewal = ref<License | null>(null),
    revokeTarget = ref<License | null>(null);
const { busy, error, run } = useOperation();
const display = computed(() => rows.value.map(row => ({ row, payload: readPayload(row.wire) })));
async function load() {
    rows.value = await request<License[]>(
        "/licenses?limit=50" + (after.value ? "&after=" + after.value : ""),
    );
}
function page(cursor: string) {
    after.value = cursor;
    void run(load);
}
function edit(row: License | null) {
    renewal.value = row;
    editing.value = true;
}
function saved() {
    editing.value = false;
    void run(load);
}
async function revoke() {
    if (!revokeTarget.value) return;
    await request("/licenses/" + revokeTarget.value.license_id + "/revoke", "POST", {
        expected_revision: revokeTarget.value.revision,
    });
    revokeTarget.value = null;
    await load();
}
function download(row: License) {
    const link = document.createElement("a");
    link.href = URL.createObjectURL(new Blob([row.wire], { type: "text/plain" }));
    link.download = row.license_id + ".pxlic";
    link.click();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
}
onMounted(() => run(load));
</script>
<template>
    <section>
        <h2>{{ t("licenses") }}</h2>
        <p class="hint">{{ t("offline") }}</p>
        <p v-if="error" role="alert" class="error">{{ t(error) }}</p>
        <div class="toolbar">
            <RecordPager
                :busy="busy || editing"
                :count="rows.length"
                @refresh="run(load)"
                @first="page('')"
                @next="page(rows[rows.length - 1]!.license_id)"
            />
            <button
                v-if="profile?.role === 'admin'"
                class="primary"
                :disabled="busy || editing"
                @click="edit(null)"
            >
                {{ t("issue") }}
            </button>
        </div>
        <LicenseForm v-if="editing" :renewal="renewal" @saved="saved" @cancel="editing = false" />
        <div v-if="revokeTarget" class="card">
            <p>{{ t("confirmRevoke") }}</p>
            <p class="mono">{{ revokeTarget.license_id }}</p>
            <div class="actions">
                <button class="danger" :disabled="busy" @click="run(revoke)">
                    {{ t("revoke") }}</button
                ><button :disabled="busy" @click="revokeTarget = null">{{ t("cancel") }}</button>
            </div>
        </div>
        <div class="table-scroll">
            <table>
                <thead>
                    <tr>
                        <th>{{ t("services") }}</th>
                        <th>{{ t("deployment") }}</th>
                        <th>{{ t("expires") }}</th>
                        <th>{{ t("revision") }}</th>
                        <th>{{ t("status") }}</th>
                        <th>{{ t("actions") }}</th>
                    </tr>
                </thead>
                <tbody>
                    <tr
                        v-for="{ row, payload } in display"
                        :key="row.license_id"
                        :data-license="row.license_id"
                    >
                        <td>
                            {{ payload.services.map(service => t(service)).join(" · ") }}<br />
                            <small>{{ t("streams") }}: {{ payload.max_streams }}</small>
                        </td>
                        <td class="mono">{{ payload.deployment_id }}<br />{{ row.license_id }}</td>
                        <td>{{ new Date(payload.expires_at * 1000).toISOString() }}</td>
                        <td>{{ row.revision }}</td>
                        <td>
                            {{
                                row.revoked_at
                                    ? t("revoked")
                                    : payload.expires_at <= Date.now() / 1000
                                      ? t("expired")
                                      : t("active")
                            }}
                        </td>
                        <td>
                            <div class="actions">
                                <button :disabled="busy || !!row.revoked_at" @click="download(row)">
                                    {{ t("download") }}
                                </button>
                                <template v-if="profile?.role === 'admin'"
                                    ><button
                                        :disabled="busy || editing || !!row.revoked_at"
                                        @click="edit(row)"
                                    >
                                        {{ t("renew") }}</button
                                    ><button
                                        class="danger"
                                        :disabled="busy || editing || !!row.revoked_at"
                                        @click="revokeTarget = row"
                                    >
                                        {{ t("revoke") }}
                                    </button></template
                                >
                            </div>
                        </td>
                    </tr>
                </tbody>
            </table>
        </div>
        <p v-if="!rows.length">{{ busy ? t("loading") : t("empty") }}</p>
    </section>
</template>
