<script setup lang="ts">
import pixelsIcon from "@/assets/icon.png";
</script>

<template>
    <span class="inline-flex items-center gap-2 font-semibold tracking-wide">
        <img :src="pixelsIcon" alt="" aria-hidden="true" class="h-[1em] w-[1em] object-contain" />
        <span>Pixels</span>
    </span>
</template>
