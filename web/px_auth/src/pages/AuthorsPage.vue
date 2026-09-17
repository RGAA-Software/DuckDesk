<script setup lang="ts">
import { onMounted, ref } from "vue";
import { request, type Profile } from "../api";
import { t } from "../uiSettings";
import { useOperation } from "../useOperation";
import RecordPager from "../components/RecordPager.vue";
type Author = Profile & { authorization_revision: number };
const rows = ref<Author[]>([]),
    username = ref(""),
    password = ref(""),
    role = ref<"admin" | "visitor">("visitor"),
    after = ref("");
const target = ref<Author | null>(null),
    replacement = ref("");
const { busy, error, run } = useOperation();
async function load() {
    rows.value = await request<Author[]>(
        "/authors?limit=50" + (after.value ? "&after=" + after.value : ""),
    );
}
async function create() {
    await request("/authors", "POST", {
        username: username.value,
        password: password.value,
        role: role.value,
    });
    password.value = "";
    username.value = "";
    await load();
}
async function reset() {
    if (!target.value) return;
    await request("/authors/" + target.value.id + "/password", "PATCH", {
        expected_revision: target.value.authorization_revision,
        password: replacement.value,
    });
    replacement.value = "";
    target.value = null;
    await load();
}
function page(cursor: string) {
    after.value = cursor;
    void run(load);
}
onMounted(() => run(load));
</script>
<template>
    <section>
        <h2>{{ t("authors") }}</h2>
        <p v-if="error" role="alert" class="error">{{ t(error) }}</p>
        <RecordPager
            :busy="busy"
            :count="rows.length"
            @refresh="run(load)"
            @first="page('')"
            @next="page(rows[rows.length - 1]!.id)"
        />
        <form class="card" @submit.prevent="run(create)">
            <h3>{{ t("createAuthor") }}</h3>
            <div class="form-grid">
                <label
                    >{{ t("username")
                    }}<input
                        v-model="username"
                        required
                        minlength="2"
                        maxlength="64"
                        autocomplete="off"
                        :disabled="busy"
                /></label>
                <label
                    >{{ t("password")
                    }}<input
                        v-model="password"
                        required
                        type="password"
                        minlength="12"
                        maxlength="256"
                        autocomplete="new-password"
                        :disabled="busy"
                /></label>
                <label
                    >{{ t("role")
                    }}<select v-model="role" :disabled="busy">
                        <option value="visitor">{{ t("visitor") }}</option>
                        <option value="admin">{{ t("admin") }}</option>
                    </select></label
                >
            </div>
            <div>
                <button class="primary" :disabled="busy">{{ t("create") }}</button>
            </div>
        </form>
        <form v-if="target" class="card" @submit.prevent="run(reset)">
            <h3>{{ t("resetPassword") }} · {{ target.username }}</h3>
            <p>{{ t("passwordNotice") }}</p>
            <label
                >{{ t("newPassword")
                }}<input
                    v-model="replacement"
                    type="password"
                    required
                    minlength="12"
                    maxlength="256"
                    autocomplete="new-password"
                    :disabled="busy"
            /></label>
            <div class="actions">
                <button class="primary" :disabled="busy">{{ t("save") }}</button
                ><button
                    type="button"
                    :disabled="busy"
                    @click="
                        target = null;
                        replacement = '';
                    "
                >
                    {{ t("cancel") }}
                </button>
            </div>
        </form>
        <div class="table-scroll">
            <table>
                <thead>
                    <tr>
                        <th>{{ t("username") }}</th>
                        <th>{{ t("role") }}</th>
                        <th>{{ t("revision") }}</th>
                        <th>{{ t("actions") }}</th>
                    </tr>
                </thead>
                <tbody>
                    <tr v-for="row in rows" :key="row.id">
                        <td>{{ row.username }}</td>
                        <td>{{ t(row.role) }}</td>
                        <td>{{ row.authorization_revision }}</td>
                        <td>
                            <button
                                :disabled="busy"
                                @click="
                                    target = row;
                                    replacement = '';
                                "
                            >
                                {{ t("resetPassword") }}
                            </button>
                        </td>
                    </tr>
                </tbody>
            </table>
        </div>
    </section>
</template>
