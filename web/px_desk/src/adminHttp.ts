import axios from "axios";
import { getBaseURL } from "@/http.ts";

const TOKEN_KEY = "pixels_desk_session";

export const getAdminToken = () => sessionStorage.getItem(TOKEN_KEY) || "";
export const setAdminToken = (token: string) => sessionStorage.setItem(TOKEN_KEY, token);
export const clearAdminToken = () => sessionStorage.removeItem(TOKEN_KEY);

const adminHttp = axios.create({
    // dev 走 vite 代理（同源，避免 CORS）；prod 同源
    baseURL: getBaseURL(),
    timeout: 20000,
});

adminHttp.interceptors.request.use(config => {
    config.headers.Authorization = `Bearer ${getAdminToken()}`;
    return config;
});

adminHttp.interceptors.response.use(
    resp => resp,
    error => {
        const status = error?.response?.status;
        if (status === 401) {
            clearAdminToken();
            if (window.location.pathname !== "/admin") {
                window.location.href = "/admin";
            }
        }
        return Promise.reject(error);
    },
);

export default adminHttp;
