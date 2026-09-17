<script setup lang="ts">
import { onMounted, ref, watch } from "vue";
import { hydrate, profile, signIn, signOut } from "./api";
import { language, theme, t } from "./uiSettings";
import { useOperation } from "./useOperation";
import CustomersPage from "./pages/CustomersPage.vue";
import LicensesPage from "./pages/LicensesPage.vue";
import AuthorsPage from "./pages/AuthorsPage.vue";
const username = ref(""),
    password = ref(""),
    tab = ref<"licenses" | "customers" | "authors">("licenses");
const { busy, error, run } = useOperation();
async function login() {
    await signIn(username.value, password.value);
    password.value = "";
}
watch(profile, value => {
    if (!value || value.role !== "admin") tab.value = "licenses";
});
onMounted(() => run(hydrate));
</script>
<template>
    <main>
        <header>
            <div>
                <h1>{{ t("title") }}</h1>
                <small>{{ t("subtitle") }}</small>
            </div>
            <div class="toolbar">
                <label
                    >{{ t("language")
                    }}<select v-model="language" :aria-label="t('language')">
                        <option value="zh">简体中文</option>
                        <option value="en">English</option>
                    </select></label
                >
                <label
                    >{{ t("theme")
                    }}<select v-model="theme" :aria-label="t('theme')">
                        <option value="light">{{ t("light") }}</option>
                        <option value="dark">{{ t("dark") }}</option>
                    </select></label
                >
                <button v-if="profile" :disabled="busy" @click="run(signOut)">
                    {{ t("logout") }}
                </button>
            </div>
        </header>
        <p v-if="error" role="alert" class="error">{{ t(error) }}</p>
        <form v-if="!profile" class="card login" @submit.prevent="run(login)">
            <h2>{{ t("login") }}</h2>
            <p class="hint">{{ t("loginHint") }}</p>
            <label
                >{{ t("username")
                }}<input
                    v-model="username"
                    required
                    minlength="2"
                    maxlength="64"
                    autocomplete="username"
                    :disabled="busy"
            /></label>
            <label
                >{{ t("password")
                }}<input
                    v-model="password"
                    type="password"
                    required
                    minlength="12"
                    maxlength="256"
                    autocomplete="current-password"
                    :disabled="busy"
            /></label>
            <button class="primary" :disabled="busy">{{ busy ? t("loading") : t("login") }}</button>
        </form>
        <template v-else>
            <p>
                {{ profile.username }} · {{ t(profile.role)
                }}<span v-if="profile.role === 'visitor'"> · {{ t("readOnly") }}</span>
            </p>
            <nav>
                <button
                    v-for="page in profile.role === 'admin'
                        ? (['licenses', 'customers', 'authors'] as const)
                        : (['licenses', 'customers'] as const)"
                    :key="page"
                    :aria-current="tab === page ? 'page' : undefined"
                    @click="tab = page"
                >
                    {{ t(page) }}
                </button>
            </nav>
            <LicensesPage v-if="tab === 'licenses'" /><CustomersPage
                v-else-if="tab === 'customers'"
            /><AuthorsPage v-else-if="profile.role === 'admin'" />
        </template>
    </main>
</template>
