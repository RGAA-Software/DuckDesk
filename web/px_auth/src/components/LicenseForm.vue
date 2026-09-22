<script setup lang="ts">
import { reactive, ref, watch } from "vue";
import { ApiFailure, request } from "../api";
import {
    licensedServices,
    products,
    readPayload,
    requestIdentity,
    validTerms,
    type License,
    type Terms,
} from "../licenseModel";
import { t } from "../uiSettings";
import { useOperation } from "../useOperation";
const props = defineProps<{ renewal: License | null }>(),
    emit = defineEmits<{ saved: []; cancel: [] }>();
const payload = props.renewal ? readPayload(props.renewal.wire) : null;
const terms = reactive<Terms>({
    customer_id: props.renewal?.customer_id ?? "",
    deployment_id: payload?.deployment_id ?? "",
    product: payload?.product ?? "pixels_console",
    distribution: payload?.distribution ?? "customer",
    release_namespace: payload?.release_namespace ?? "pixels.customer",
    oem_id: payload?.oem_id ?? null,
    machine_sha256: payload?.machine_sha256 ?? "",
    mode: payload?.mode ?? "licensed",
    activation: { kind: "immediately" },
    expires_at: 0,
    max_streams: payload?.max_streams ?? 1,
    services: payload?.services ?? ["cloud_applications", "desktop", "rdp"],
});
watch(
    () => [terms.distribution, terms.oem_id] as const,
    ([distribution, oemId]) => {
        if (distribution === "official") {
            terms.release_namespace = "pixels.official";
            terms.oem_id = null;
        } else if (distribution === "customer") {
            terms.release_namespace = "pixels.customer";
            terms.oem_id = null;
        } else {
            terms.release_namespace = oemId ? `oem.${oemId}` : "";
        }
    },
    { immediate: true },
);
const expires = ref(
    new Date(Math.max((payload?.expires_at ?? 0) * 1000, Date.now()) + 30 * 86400000)
        .toISOString()
        .slice(0, 16),
);
const identity = requestIdentity(),
    requestId = ref(""),
    { busy, error, run } = useOperation();
async function save() {
    terms.expires_at = Date.parse(expires.value + ":00Z") / 1000;
    terms.services.sort();
    if (!validTerms(terms)) throw new ApiFailure("invalid");
    const operation = props.renewal
        ? {
              operation: "renew",
              license_id: props.renewal.license_id,
              expected_revision: props.renewal.revision,
              terms,
          }
        : { operation: "create", terms };
    requestId.value = identity.next(operation);
    await request("/licenses/issue", "POST", { request_id: requestId.value, request: operation });
    identity.reset();
    emit("saved");
}
</script>
<template>
    <form class="card" @submit.prevent="run(save)">
        <h3>{{ renewal ? t("renew") : t("issue") }}</h3>
        <p v-if="renewal" class="hint">{{ t("renewHint") }}</p>
        <p v-if="error" role="alert" class="error">{{ t(error) }}</p>
        <div class="form-grid">
            <label
                >{{ t("customerId")
                }}<input v-model="terms.customer_id" required :disabled="busy || !!renewal"
            /></label>
            <label
                >{{ t("deployment")
                }}<input v-model="terms.deployment_id" required :disabled="busy || !!renewal"
            /></label>
            <label
                >{{ t("product")
                }}<select v-model="terms.product" :disabled="busy || !!renewal">
                    <option v-for="p in products" :key="p" :value="p">{{ t(p) }}</option>
                </select></label
            >
            <label
                >{{ t("distribution")
                }}<select v-model="terms.distribution" :disabled="busy || !!renewal">
                    <option value="official">{{ t("official") }}</option>
                    <option value="customer">{{ t("customer") }}</option>
                    <option value="oem">{{ t("oem") }}</option>
                </select></label
            >
            <label v-if="terms.distribution === 'oem'"
                >{{ t("oemId")
                }}<input
                    v-model="terms.oem_id"
                    required
                    pattern="[a-z0-9](?:[a-z0-9-]{1,30}[a-z0-9])"
                    :disabled="busy || !!renewal"
            /></label>
            <label
                >{{ t("releaseNamespace")
                }}<input v-model="terms.release_namespace" readonly disabled
            /></label>
            <label
                >{{ t("machine")
                }}<input
                    v-model="terms.machine_sha256"
                    required
                    pattern="[a-f0-9]{64}"
                    :disabled="busy || !!renewal"
            /></label>
            <label
                >{{ t("expires")
                }}<input v-model="expires" type="datetime-local" required :disabled="busy"
            /></label>
            <label
                >{{ t("mode")
                }}<select v-model="terms.mode" :disabled="busy">
                    <option value="licensed">{{ t("licensed") }}</option>
                    <option value="trial">{{ t("trial") }}</option>
                </select></label
            >
            <label
                >{{ t("streams")
                }}<input
                    v-model.number="terms.max_streams"
                    type="number"
                    min="1"
                    max="4294967295"
                    required
                    :disabled="busy"
            /></label>
        </div>
        <fieldset :disabled="busy">
            <legend>{{ t("services") }}</legend>
            <label v-for="licensedService in licensedServices" :key="licensedService"
                ><input v-model="terms.services" type="checkbox" :value="licensedService" />{{
                    t(licensedService)
                }}</label
            >
        </fieldset>
        <p v-if="requestId" class="mono">{{ t("requestId") }}: {{ requestId }}</p>
        <div class="actions">
            <button class="primary" :disabled="busy">{{ t("save") }}</button
            ><button type="button" :disabled="busy" @click="emit('cancel')">
                {{ t("cancel") }}
            </button>
        </div>
    </form>
</template>
