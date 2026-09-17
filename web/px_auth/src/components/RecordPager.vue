<script setup lang="ts">
import { t } from "../uiSettings";
defineProps<{ busy: boolean; count: number }>();
defineEmits<{ refresh: []; first: []; next: [] }>();
</script>
<template>
    <div class="toolbar">
        <button :disabled="busy" @click="$emit('refresh')">{{ t("refresh") }}</button>
        <button :disabled="busy" @click="$emit('first')">{{ t("first") }}</button>
        <button :disabled="busy || count < 50" @click="$emit('next')">{{ t("next") }}</button>
    </div>
</template>
