import axios, { type AxiosInstance } from "axios";

const USER_TOKEN_KEY = "pixels.user_web.token";
const GUEST_TOKEN_KEY = "pixels.user_web.guest_token";
export const USER_SESSION_EXPIRED_EVENT = "px-user-session-expired";

type SubjectKind = "user" | "guest";

function createClient(tokenKey?: string, subject?: SubjectKind): AxiosInstance {
    const client = axios.create({
        baseURL: "",
        timeout: 30000,
    });
    client.interceptors.request.use(config => {
        config.headers.set("X-Pixels-Client-Type", "user_web");
        const token = tokenKey ? sessionStorage.getItem(tokenKey) : null;
        if (token) config.headers.set("Authorization", `Bearer ${token}`);
        if (subject) config.headers.set("X-Pixels-Subject-Kind", subject);
        return config;
    });
    client.interceptors.response.use(
        response => response,
        error => {
            if (error?.response?.status === 401) {
                if (tokenKey) sessionStorage.removeItem(tokenKey);
                if (subject === "user") window.dispatchEvent(new Event(USER_SESSION_EXPIRED_EVENT));
            }
            return Promise.reject(error);
        },
    );
    return client;
}

export const userHttp = createClient(USER_TOKEN_KEY);
export const userResourceHttp = createClient(USER_TOKEN_KEY, "user");
export const guestHttp = createClient(GUEST_TOKEN_KEY);
export const guestResourceHttp = createClient(GUEST_TOKEN_KEY, "guest");
export const publicHttp = createClient();

export function setUserToken(token: string) {
    if (token) sessionStorage.setItem(USER_TOKEN_KEY, token);
    else sessionStorage.removeItem(USER_TOKEN_KEY);
}

export function hasUserToken() {
    return Boolean(sessionStorage.getItem(USER_TOKEN_KEY));
}

export function setGuestToken(token: string) {
    if (token) sessionStorage.setItem(GUEST_TOKEN_KEY, token);
    else sessionStorage.removeItem(GUEST_TOKEN_KEY);
}

export function hasGuestToken() {
    return Boolean(sessionStorage.getItem(GUEST_TOKEN_KEY));
}
