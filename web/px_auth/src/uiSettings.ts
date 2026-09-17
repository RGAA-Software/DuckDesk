import { ref, watch } from "vue";
import { en, type TextKey } from "./locales/en";
import { zh } from "./locales/zh";
export const language = ref<"en" | "zh">(
    localStorage.getItem("pixels_auth_language") === "en" ? "en" : "zh",
);
export const theme = ref<"light" | "dark">(
    localStorage.getItem("pixels_auth_theme") === "light" ? "light" : "dark",
);
export function t(key: TextKey): string {
    return (language.value === "zh" ? zh : en)[key];
}
watch(
    language,
    value => {
        localStorage.setItem("pixels_auth_language", value);
        document.documentElement.lang = value;
    },
    { immediate: true },
);
watch(
    theme,
    value => {
        localStorage.setItem("pixels_auth_theme", value);
        document.documentElement.dataset.theme = value;
    },
    { immediate: true },
);
