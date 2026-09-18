import axios from "axios";

// 获取基础URL
const getBaseURL = () => {
    const { protocol, hostname, port } = window.location;

    // 开发模式走 Vite 代理（同源，见 vite.config.ts 的 proxy），保持相对路径即可
    if (import.meta.env.DEV) {
        return "";
    }

    const basePort = port ? `:${port}` : "";
    return `${protocol}//${hostname}${basePort}`;
};

const getHostPort = () => {
    const { hostname, port } = window.location;

    // 开发模式 WebSocket 也走 Vite 代理（/console 已配置 ws:true）
    if (import.meta.env.DEV) {
        return window.location.host;
    }

    const basePort = port ? `:${port}` : "";
    return `${hostname}${basePort}`;
};

// 导出 baseURL 常量
export const BASE_URL = getBaseURL();
export const HOST_PORT = getHostPort();

const axiosHttp = axios.create({
    baseURL: getBaseURL(),
    timeout: 30000,
});

const ADMIN_TOKEN_KEY = "pixels.admin_web.token";

export function setAdminToken(token: string) {
    if (token) sessionStorage.setItem(ADMIN_TOKEN_KEY, token);
    else sessionStorage.removeItem(ADMIN_TOKEN_KEY);
}

export function hasAdminToken(): boolean {
    return Boolean(sessionStorage.getItem(ADMIN_TOKEN_KEY));
}

axiosHttp.interceptors.request.use(config => {
    config.headers.set("X-Pixels-Client-Type", "admin_web");
    const token = sessionStorage.getItem(ADMIN_TOKEN_KEY);
    if (token) config.headers.set("Authorization", `Bearer ${token}`);
    return config;
});

axiosHttp.interceptors.response.use(
    response => response,
    error => {
        if (error?.response?.status === 401) setAdminToken("");
        return Promise.reject(error);
    },
);

export default axiosHttp;
