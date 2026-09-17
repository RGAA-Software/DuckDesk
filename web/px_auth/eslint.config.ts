import { globalIgnores } from "eslint/config";
import {
    configureVueProject,
    defineConfigWithVueTs,
    vueTsConfigs,
} from "@vue/eslint-config-typescript";
import { fileURLToPath } from "node:url";
import pluginVue from "eslint-plugin-vue";

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
        name: "pixels/microsoft-typescript-style",
        files: ["**/*.{ts,mts,cts,tsx,vue}"],
        languageOptions: {
            parserOptions: { tsconfigRootDir: fileURLToPath(new URL(".", import.meta.url)) },
        },
        rules: { "brace-style": ["error", "stroustrup", { allowSingleLine: true }] },
    },
);
