<script setup lang="ts">
import { onMounted, ref } from "vue";
import { useI18n } from "vue-i18n";
import {
    getManagedLicenseStatus,
    installManagedLicense,
    type LicensedService,
    type ManagedLicenseStatus,
} from "@/model/managed_license_api";

const { locale, t } = useI18n();
const licenseStatus = ref<ManagedLicenseStatus | null>(null);
const loading = ref(false);
const unavailable = ref(false);
const importing = ref(false);
const importFailed = ref(false);

async function refresh() {
    loading.value = true;
    await getManagedLicenseStatus()
        .then(status => {
            licenseStatus.value = status;
            unavailable.value = false;
        })
        .catch(() => {
            licenseStatus.value = null;
            unavailable.value = true;
        })
        .finally(() => {
            loading.value = false;
        });
}

async function importLicense(event: Event): Promise<void> {
    if (!(event.target instanceof HTMLInputElement)) return;
    const licenseFile = event.target.files?.[0];
    if (!licenseFile) return;
    event.target.value = "";
    if (licenseFile.size === 0 || licenseFile.size > 4096) {
        importFailed.value = true;
        return;
    }
    importing.value = true;
    importFailed.value = false;
    try {
        licenseStatus.value = await installManagedLicense(await licenseFile.text());
        unavailable.value = false;
    } catch {
        importFailed.value = true;
    } finally {
        importing.value = false;
    }
}

function serviceLabel(service: LicensedService): string {
    const serviceKeys: Record<LicensedService, string> = {
        cloud_applications: "dashboard.licenseServices.cloudApplications",
        desktop: "dashboard.licenseServices.desktop",
        rdp: "dashboard.licenseServices.rdp",
    };
    return t(serviceKeys[service] ?? "dashboard.unknown");
}

function expirationLabel(unixSeconds: number): string {
    const expiration = new Date(unixSeconds * 1000);
    if (Number.isNaN(expiration.getTime())) return t("dashboard.unknown");
    return new Intl.DateTimeFormat(locale.value, {
        dateStyle: "medium",
        timeStyle: "medium",
    }).format(expiration);
}

onMounted(refresh);
</script>

<template>
    <a-card :title="t('dashboard.licenseTitle')" :loading="loading">
        <template #extra>
            <a-button @click="refresh">{{ t("dashboard.refresh") }}</a-button>
        </template>
        <a-alert
            v-if="unavailable"
            type="warning"
            show-icon
            :message="t('dashboard.licenseUnavailable')"
        />
        <a-alert v-else-if="!licenseStatus" type="info" show-icon :message="t('dashboard.licenseNotActivated')" />
        <a-descriptions v-else bordered size="small" :column="2">
            <a-descriptions-item :label="t('dashboard.licenseServicesLabel')">
                {{ licenseStatus.services.map(serviceLabel).join(", ") }}
            </a-descriptions-item>
            <a-descriptions-item :label="t('dashboard.licenseMaxStreams')">
                {{ licenseStatus.max_streams }}
            </a-descriptions-item>
            <a-descriptions-item :label="t('dashboard.licenseExpiresAt')">
                {{ expirationLabel(licenseStatus.expires_at) }}
            </a-descriptions-item>
            <a-descriptions-item :label="t('dashboard.licenseRevision')">
                {{ licenseStatus.revision }}
            </a-descriptions-item>
            <a-descriptions-item :label="t('dashboard.licenseId')" :span="2">
                {{ licenseStatus.license_id }}
            </a-descriptions-item>
        </a-descriptions>
        <div class="license-import">
            <label for="license-file">{{ t("dashboard.licenseImport") }}</label>
            <input id="license-file" type="file" accept=".pxlic,.txt" :disabled="importing" @change="importLicense" />
            <a-alert v-if="importFailed" type="error" show-icon :message="t('dashboard.licenseImportFailed')" />
        </div>
    </a-card>
</template>

<style scoped>
.license-import { display: flex; flex-direction: column; gap: 8px; margin-top: 16px; }
</style>
