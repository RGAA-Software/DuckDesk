import axiosHttp from "@/http";

export type LicensedService = "cloud_applications" | "desktop" | "rdp";

export interface ManagedLicenseStatus {
    license_id: string;
    revision: number;
    expires_at: number;
    max_streams: number;
    services: LicensedService[];
}

export async function getManagedLicenseStatus(): Promise<ManagedLicenseStatus | null> {
    const response = await axiosHttp.get<ManagedLicenseStatus | null>("/api/console/managed/license");
    return response.data;
}

export async function installManagedLicense(wire: string): Promise<ManagedLicenseStatus> {
    const response = await axiosHttp.put<ManagedLicenseStatus>("/api/console/managed/license", { wire });
    return response.data;
}
