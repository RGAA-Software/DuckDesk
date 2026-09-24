<script setup lang="ts">
import { onMounted, ref } from "vue";
import { useI18n } from "vue-i18n";
import {
    getManagedLicenseStatus,
    type LicensedService,
    type ManagedLicenseStatus,
} from "@/model/managed_license_api";

const { locale, t } = useI18n();
const licenseStatus = ref<ManagedLicenseStatus>();
const loading = ref(false);
const unavailable = ref(false);

async function refresh() {
    loading.value = true;
    await getManagedLicenseStatus()
        .then(status => {
            licenseStatus.value = status;
            unavailable.value = false;
        })
        .catch(() => {
            licenseStatus.value = undefined;
            unavailable.value = true;
        })
        .finally(() => {
            loading.value = false;
        });
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
        <a-descriptions v-else-if="licenseStatus" bordered size="small" :column="2">
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
    </a-card>
</template>
