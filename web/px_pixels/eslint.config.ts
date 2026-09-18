import { globalIgnores } from "eslint/config";
import {
    configureVueProject,
    defineConfigWithVueTs,
    vueTsConfigs,
} from "@vue/eslint-config-typescript";
import { fileURLToPath } from "node:url";
import pluginVue from "eslint-plugin-vue";
import pluginVitest from "@vitest/eslint-plugin";
import pluginPlaywright from "eslint-plugin-playwright";
import skipFormatting from "@vue/eslint-config-prettier/skip-formatting";

configureVueProject({ rootDir: fileURLToPath(new URL(".", import.meta.url)) });

export default defineConfigWithVueTs(
    {
        name: "app/files-to-lint",
        files: ["**/*.{ts,mts,tsx,vue}"],
    },

    globalIgnores(["**/dist/**", "**/dist-ssr/**", "**/coverage/**"]),

    pluginVue.configs["flat/essential"],
    vueTsConfigs.recommended,

    {
        ...pluginVitest.configs.recommended,
        files: ["src/**/__tests__/*"],
    },

    {
        ...pluginPlaywright.configs["flat/recommended"],
        files: ["e2e/**/*.{test,spec}.{js,ts,jsx,tsx}"],
    },
    skipFormatting,
    {
        name: "pixels/microsoft-typescript-style",
        files: ["**/*.{ts,mts,cts,tsx,vue}"],
        languageOptions: {
            parserOptions: { tsconfigRootDir: fileURLToPath(new URL(".", import.meta.url)) },
        },
        rules: { "brace-style": ["error", "stroustrup", { allowSingleLine: true }] },
    },
);
