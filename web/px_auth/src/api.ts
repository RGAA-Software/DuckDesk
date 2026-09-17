import { ref } from "vue";
import type { TextKey } from "./locales/en";
export type Profile = { id: string; username: string; role: "admin" | "visitor" };
export const profile = ref<Profile | null>(null);
const tokenKey = "pixels_auth_session";
export function clearSession() {
    sessionStorage.removeItem(tokenKey);
    profile.value = null;
}
export class ApiFailure extends Error {
    constructor(
        public key: TextKey,
        public status = 0,
    ) {
        super(key);
    }
}
export async function request<T>(path: string, method = "GET", body?: unknown): Promise<T> {
    const token = sessionStorage.getItem(tokenKey);
    let response: Response;
    try {
        response = await fetch("/api/auth" + path, {
            method,
            headers: {
                "Content-Type": "application/json",
                ...(token ? { Authorization: "Bearer " + token } : {}),
            },
            body: body === undefined ? undefined : JSON.stringify(body),
            signal: AbortSignal.timeout(20000),
            credentials: "omit",
        });
    }
 catch {
        throw new ApiFailure("unavailable");
    }
    if (!response.ok) {
        if (response.status === 401 && path !== "/sessions") clearSession();
        throw new ApiFailure(
            response.status === 401
                ? "unauthorized"
                : response.status === 409
                  ? "conflict"
                  : response.status === 429
                    ? "rateLimited"
                    : response.status === 400 || response.status === 422
                      ? "invalid"
                      : response.status >= 500
                        ? "unavailable"
                        : "failed",
            response.status,
        );
    }
    if (response.status === 204) return undefined as T;
    return response.json() as Promise<T>;
}
export async function signIn(username: string, password: string) {
    const result = await request<{ token: string; profile: Profile }>("/sessions", "POST", {
        username,
        password,
    });
    sessionStorage.setItem(tokenKey, result.token);
    profile.value = result.profile;
}
export async function signOut() {
    await request("/session", "DELETE");
    clearSession();
}
export async function hydrate() {
    if (sessionStorage.getItem(tokenKey)) profile.value = await request<Profile>("/me");
}
export function errorKey(error: unknown): TextKey {
    return error instanceof ApiFailure ? error.key : "failed";
}
