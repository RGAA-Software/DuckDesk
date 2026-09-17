import { ref } from "vue";
import { errorKey } from "./api";
import type { TextKey } from "./locales/en";
export function useOperation() {
    const busy = ref(false),
        error = ref<TextKey | null>(null);
    async function run(action: () => Promise<void>): Promise<boolean> {
        if (busy.value) return false;
        busy.value = true;
        error.value = null;
        try {
            await action();
            return true;
        }
 catch (cause) {
            error.value = errorKey(cause);
            return false;
        }
 finally {
            busy.value = false;
        }
    }
    return { busy, error, run };
}
