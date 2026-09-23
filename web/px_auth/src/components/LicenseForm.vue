<script setup lang="ts">
import { reactive, ref } from "vue";
import { ApiFailure, request } from "../api";
import {
    licensedServices,
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
    expires_at: 0,
    max_streams: payload?.max_streams ?? 1,
    services: payload?.services ?? ["cloud_applications", "desktop", "rdp"],
});
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
                >{{ t("expires")
                }}<input v-model="expires" type="datetime-local" required :disabled="busy"
            /></label>
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
