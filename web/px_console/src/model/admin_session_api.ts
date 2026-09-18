import axiosHttp, { hasAdminToken, setAdminToken } from "@/http.ts";

export interface AdminProfile {
    id: string;
    username: string;
    role: "admin" | "viewer";
    authorization_revision: number;
    revision: number;
    avatar_url: string | null;
    created_at: string;
}

export async function loginAdmin(username: string, password: string): Promise<AdminProfile | null> {
    setAdminToken("");
    const response = await axiosHttp.post<{ token: string; profile: AdminProfile }>(
        "/api/console/sessions",
        { username, password },
    );
    if (response.status !== 200 || !response.data?.token || !response.data.profile) return null;
    setAdminToken(response.data.token);
    return response.data.profile;
}

export async function queryAdminSession(): Promise<AdminProfile | null> {
    if (!hasAdminToken()) return null;
    try {
        const response = await axiosHttp.get<AdminProfile>("/api/console/session");
        return response.status === 200 ? response.data : null;
    } catch {
        return null;
    }
}

export async function logoutAdmin(): Promise<void> {
    try {
        if (hasAdminToken()) await axiosHttp.delete("/api/console/session");
    } finally {
        setAdminToken("");
    }
}
