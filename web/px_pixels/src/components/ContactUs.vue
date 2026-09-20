<script setup lang="ts">
import { computed, ref, watch } from "vue";
import { useI18n } from "vue-i18n";
import { ElNotification } from "element-plus";
import type { FormInstance, FormRules } from "element-plus";
import { IconChevronDown, IconX } from "@tabler/icons-vue";
import axiosHttp from "@/http.ts";
import { submissionIdentity } from "@/submission";

const { t } = useI18n();

interface Consult {
    title: string;
    yourName: string;
    consultType: string;
    content: string;
    email: string;
    wechat: string;
    qq: string;
}

const emptyConsult = (): Consult => ({
    title: "",
    yourName: "",
    consultType: "",
    content: "",
    email: "",
    wechat: "",
    qq: "",
});

const consult = ref<Consult>(emptyConsult());
const formRef = ref<FormInstance>();
const submitting = ref(false);
const requestId = submissionIdentity();

const rules = computed<FormRules<Consult>>(() => ({
    title: [{ required: true, message: t("consult.required"), trigger: "blur" }],
    yourName: [{ required: true, message: t("consult.required"), trigger: "blur" }],
    consultType: [{ required: true, message: t("consult.required"), trigger: "change" }],
    content: [{ required: true, message: t("consult.required"), trigger: "blur" }],
    email: [
        { required: true, message: t("consult.required"), trigger: "blur" },
        { type: "email", message: t("consult.emailInvalid"), trigger: "blur" },
    ],
}));

// 父组件传入的 v-model
const props = defineProps<{
    modelValue: boolean;
    initialTitle?: string;
    initialContent?: string;
    initialConsultType?: string;
}>();

const emit = defineEmits<{
    (eventName: "update:modelValue", value: boolean): void;
}>();

// 内部代理状态（关键）
const visible = computed({
    get: () => props.modelValue,
    set: val => emit("update:modelValue", val),
});

watch(
    () => props.modelValue,
    isVisible => {
        if (!isVisible) return;
        if (props.initialTitle) consult.value.title = props.initialTitle;
        if (props.initialContent) consult.value.content = props.initialContent;
        if (props.initialConsultType) {
            consult.value.consultType = props.initialConsultType;
        }
    },
);

const close = () => {
    visible.value = false;
};

async function confirm() {
    if (!formRef.value || submitting.value) return;
    const valid = await formRef.value.validate().catch(() => false);
    if (!valid) return;

    submitting.value = true;
    try {
        const body = {
            title: consult.value.title,
            your_name: consult.value.yourName,
            consult_type: consult.value.consultType,
            description: consult.value.content,
            email: consult.value.email,
            wechat: consult.value.wechat,
            qq: consult.value.qq,
        };
        await axiosHttp.post("/api/desk/consults", { ...body, request_id: requestId.next(body) });
        requestId.reset();

        ElNotification({
            title: t("consult.successTitle"),
            message: t("consult.successMessage"),
            type: "primary",
        });
        visible.value = false;
        consult.value = emptyConsult();
        formRef.value?.clearValidate();
    }
 catch {
        ElNotification({
            title: t("consult.failTitle"),
            message: t("consult.failMessage"),
            type: "warning",
        });
    }
 finally {
        submitting.value = false;
    }
}
</script>

<template>
    <el-dialog
        v-model="visible"
        align-center
        append-to-body
        :show-close="false"
        class="pixels-contact-dialog !max-w-[92vw] !w-140"
    >
        <template #header>
            <div class="pixels-contact-header">
                <span class="pixels-contact-title">{{ t("consult.dialogTitle") }}</span>
                <button type="button" :aria-label="t('consult.cancel')" @click="close">
                    <IconX :size="20" :stroke-width="1.7" />
                </button>
            </div>
        </template>

        <el-form ref="formRef" :model="consult" :rules="rules" label-width="auto">
            <el-form-item :label="t('consult.title')" prop="title">
                <el-input v-model="consult.title" />
            </el-form-item>

            <el-form-item :label="t('consult.yourName')" prop="yourName">
                <el-input v-model="consult.yourName" />
            </el-form-item>

            <el-form-item :label="t('consult.type')" prop="consultType">
                <el-select
                    v-model="consult.consultType"
                    :suffix-icon="IconChevronDown"
                    :placeholder="t('consult.typePlaceholder')"
                >
                    <el-option :label="t('consult.typePersonal')" value="personal" />
                    <el-option :label="t('consult.typeEnterprise')" value="enterprise" />
                </el-select>
            </el-form-item>

            <el-form-item :label="t('consult.content')" prop="content">
                <el-input
                    v-model="consult.content"
                    :rows="2"
                    type="textarea"
                    :placeholder="t('consult.contentPlaceholder')"
                />
            </el-form-item>

            <el-form-item :label="t('consult.email')" prop="email">
                <el-input v-model="consult.email" />
            </el-form-item>

            <el-form-item :label="t('consult.wechat')" prop="wechat">
                <el-input v-model="consult.wechat" />
            </el-form-item>

            <el-form-item :label="t('consult.qq')" prop="qq">
                <el-input v-model="consult.qq" />
            </el-form-item>
        </el-form>

        <template #footer>
            <div class="dialog-footer">
                <el-button @click="close">{{ t("consult.cancel") }}</el-button>
                <el-button type="primary" :loading="submitting" @click="confirm">
                    {{ t("consult.submit") }}
                </el-button>
            </div>
        </template>
    </el-dialog>
</template>

<style scoped>
.pixels-contact-title {
    color: var(--text);
    font-size: 18px;
    font-weight: 700;
}

.pixels-contact-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
}

.pixels-contact-header button {
    display: grid;
    width: 34px;
    height: 34px;
    place-items: center;
    border: 0;
    border-radius: 8px;
    background: transparent;
    color: var(--muted);
    cursor: pointer;
}

.pixels-contact-header button:hover {
    background: var(--bg2);
    color: var(--text);
}

:global(.pixels-contact-dialog) {
    --el-color-primary: var(--brand);
    --el-dialog-bg-color: var(--panel);
    padding: 24px;
    border: 1px solid var(--line);
    border-radius: 18px !important;
    box-shadow: 0 28px 80px rgba(24, 24, 27, 0.18) !important;
}

:global(.pixels-contact-dialog .el-dialog__header) {
    padding-bottom: 14px;
}

:global(.pixels-contact-dialog .el-form-item__label) {
    color: var(--text);
    font-family: var(--font-ui);
}

:global(.pixels-contact-dialog .el-input__wrapper),
:global(.pixels-contact-dialog .el-select__wrapper),
:global(.pixels-contact-dialog .el-textarea__inner) {
    border-radius: 9px !important;
    background: var(--bg2) !important;
    box-shadow: 0 0 0 1px var(--line) inset !important;
    color: var(--text);
}

:global(.pixels-contact-dialog .el-button) {
    --bb: transparent;
    --bf: transparent;
    height: 40px;
    padding: 0 17px;
    border: 1px solid var(--line) !important;
    border-radius: 9px !important;
    clip-path: none !important;
    background: var(--panel) !important;
    color: var(--text) !important;
    font: 650 13px var(--font-ui);
    letter-spacing: 0;
}

:global(.pixels-contact-dialog .el-button::before),
:global(.pixels-contact-dialog .el-button::after) {
    display: none !important;
}

:global(.pixels-contact-dialog .el-button--primary) {
    border-color: var(--brand) !important;
    background: var(--brand) !important;
    color: #ffffff !important;
}
</style>
