<script setup lang="ts">
import { onMounted, reactive, ref } from "vue";
import { message } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import { useManagementRefresh } from "@/model/management_events.ts";
import {
    configureManagedRelay,
    createManagedRelay,
    listManagedRelays,
    type ManagedRelay,
} from "@/model/managed_relay_api";
import { copyText } from "@/util/clipboard";

const { locale, t } = useI18n();
const relays = ref<ManagedRelay[]>([]);
const loading = ref(false);
const saving = ref(false);
const editorOpen = ref(false);
const editing = ref<ManagedRelay>();
const credentialOpen = ref(false);
const relayToken = ref("");
const form = reactive({
    name: "",
    publicHost: "",
    publicPort: 4605,
    draining: true,
    disabled: false,
});

async function refresh(): Promise<void> {
    loading.value = true;
    try {
        relays.value = await listManagedRelays();
    } finally {
        loading.value = false;
    }
}

function create(): void {
    editing.value = undefined;
    Object.assign(form, {
        name: "",
        publicHost: "",
        publicPort: 4605,
        draining: true,
        disabled: false,
    });
    editorOpen.value = true;
}

function edit(relay: ManagedRelay): void {
    editing.value = relay;
    Object.assign(form, {
        name: relay.name,
        publicHost: relay.public_host,
        publicPort: relay.public_port,
        draining: relay.desired_draining,
        disabled: relay.disabled,
    });
    editorOpen.value = true;
}

async function save(): Promise<void> {
    if (!editing.value && (!form.name.trim() || !form.publicHost.trim())) {
        message.error(t("relays.validation.identity"));
        return;
    }
    saving.value = true;
    try {
        if (editing.value) {
            await configureManagedRelay(editing.value, {
                draining: form.draining,
                disabled: form.disabled,
            });
        } else {
            const created = await createManagedRelay(form.name, form.publicHost, form.publicPort);
            relayToken.value = created.relay_token;
            credentialOpen.value = true;
        }
        editorOpen.value = false;
        await refresh();
    } finally {
        saving.value = false;
    }
}

async function copyCredential(): Promise<void> {
    await copyText(relayToken.value);
    message.success(t("relays.messages.copied"));
}

function eligibilityKey(relay: ManagedRelay): string {
    if (relay.disabled) return "relays.reasons.disabled";
    if (!relay.fresh || relay.state !== "ready") return "relays.reasons.offline";
    if (relay.desired_draining) return "relays.reasons.desiredDraining";
    if (relay.reported_draining === null) return "relays.reasons.unknownDrain";
    if (relay.reported_draining) return "relays.reasons.reportedDraining";
    if (
        relay.max_connections === null ||
        relay.current_connections === null ||
        relay.max_rooms === null ||
        relay.current_rooms === null
    ) {
        return "relays.reasons.unknownCapacity";
    }
    if (
        relay.current_connections + 2 > relay.max_connections ||
        relay.current_rooms + 1 > relay.max_rooms
    ) {
        return "relays.reasons.full";
    }
    return "relays.reasons.eligible";
}

function isEligible(relay: ManagedRelay): boolean {
    return eligibilityKey(relay) === "relays.reasons.eligible";
}

function formatCapacity(current: number | null, maximum: number | null): string {
    return current === null || maximum === null ? t("relays.unknown") : `${current} / ${maximum}`;
}

function formatBytes(bytes: number | null): string {
    if (bytes === null) return t("relays.unknown");
    const units = ["B", "KiB", "MiB", "GiB", "TiB"];
    let value = bytes;
    let unitIndex = 0;
    while (value >= 1024 && unitIndex < units.length - 1) {
        value /= 1024;
        unitIndex += 1;
    }
    return `${value.toFixed(unitIndex === 0 ? 0 : 1)} ${units[unitIndex]}`;
}

function formatTimestamp(timestamp: string | null): string {
    if (!timestamp) return t("relays.unknown");
    return new Intl.DateTimeFormat(locale.value, {
        dateStyle: "medium",
        timeStyle: "medium",
    }).format(new Date(timestamp));
}

onMounted(refresh);
useManagementRefresh(["relays"], refresh);
</script>

<template>
    <a-card :title="t('relays.title')">
        <template #extra>
            <a-button type="primary" @click="create">{{ t("relays.create") }}</a-button>
        </template>
        <a-alert
            type="info"
            show-icon
            :message="t('relays.statusNotice')"
            style="margin-bottom: 12px"
        />
        <a-table :data-source="relays" row-key="id" :loading="loading" :pagination="false">
            <a-table-column :title="t('relays.name')" data-index="name" />
            <a-table-column :title="t('relays.endpoint')">
                <template #default="{ record }"
                    >{{ record.public_host }}:{{ record.public_port }}</template
                >
            </a-table-column>
            <a-table-column :title="t('relays.state')">
                <template #default="{ record }">
                    <a-tag :color="isEligible(record) ? 'green' : 'orange'">
                        {{ t(eligibilityKey(record)) }}
                    </a-tag>
                </template>
            </a-table-column>
            <a-table-column :title="t('relays.connections')">
                <template #default="{ record }">
                    {{ formatCapacity(record.current_connections, record.max_connections) }}
                </template>
            </a-table-column>
            <a-table-column :title="t('relays.rooms')">
                <template #default="{ record }">
                    {{ formatCapacity(record.current_rooms, record.max_rooms) }}
                </template>
            </a-table-column>
            <a-table-column :title="t('relays.drainState')">
                <template #default="{ record }">
                    {{ record.desired_draining ? t("relays.on") : t("relays.off") }} /
                    {{
                        record.reported_draining === null
                            ? t("relays.unknown")
                            : record.reported_draining
                              ? t("relays.on")
                              : t("relays.off")
                    }}
                </template>
            </a-table-column>
            <a-table-column :title="t('relays.traffic')">
                <template #default="{ record }">
                    {{ formatBytes(record.uploaded_bytes) }} /
                    {{ formatBytes(record.forwarded_bytes) }}
                </template>
            </a-table-column>
            <a-table-column :title="t('relays.lastSeen')">
                <template #default="{ record }">{{ formatTimestamp(record.last_seen) }}</template>
            </a-table-column>
            <a-table-column :title="t('relays.version')">
                <template #default="{ record }">
                    {{ record.product_version_code ?? t("relays.unknown") }}
                </template>
            </a-table-column>
            <a-table-column :title="t('identity.users.actions')">
                <template #default="{ record }">
                    <a-button size="small" @click="edit(record)">
                        {{ t("identity.actions.edit") }}
                    </a-button>
                </template>
            </a-table-column>
            <template #emptyText>{{ t("relays.empty") }}</template>
        </a-table>
    </a-card>

    <a-modal
        v-model:open="editorOpen"
        :title="t(editing ? 'relays.edit' : 'relays.create')"
        :confirm-loading="saving"
        @ok="save"
    >
        <a-form layout="vertical">
            <a-form-item :label="t('relays.name')">
                <a-input v-model:value="form.name" :disabled="!!editing" />
            </a-form-item>
            <a-form-item :label="t('relays.host')">
                <a-input v-model:value="form.publicHost" :disabled="!!editing" />
            </a-form-item>
            <a-form-item :label="t('relays.port')">
                <a-input-number
                    v-model:value="form.publicPort"
                    :disabled="!!editing"
                    :min="1"
                    :max="65535"
                />
            </a-form-item>
            <a-form-item v-if="editing" :label="t('relays.draining')">
                <a-switch v-model:checked="form.draining" />
            </a-form-item>
            <a-form-item v-if="editing" :label="t('relays.disabled')">
                <a-switch v-model:checked="form.disabled" />
            </a-form-item>
        </a-form>
    </a-modal>

    <a-modal v-model:open="credentialOpen" :title="t('relays.credentialTitle')" :footer="null">
        <a-alert type="warning" show-icon :message="t('relays.credentialNotice')" />
        <a-typography-paragraph copyable style="margin-top: 16px; word-break: break-all">
            {{ relayToken }}
        </a-typography-paragraph>
        <a-button type="primary" @click="copyCredential">
            {{ t("relays.copyCredential") }}
        </a-button>
    </a-modal>
</template>
