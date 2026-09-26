import { beforeEach, describe, expect, it, vi } from "vitest";
import axiosHttp from "@/http";
import {
    getManagedLicenseStatus,
    installManagedLicense,
    type ManagedLicenseStatus,
} from "./managed_license_api";

vi.mock("@/http", () => ({
    default: { get: vi.fn(), put: vi.fn() },
}));

describe("managed license API", () => {
    beforeEach(() => vi.clearAllMocks());

    it("reads the administrator-only license status without sending credentials in the URL", async () => {
        const licenseStatus: ManagedLicenseStatus = {
            license_id: "00000000-0000-0000-0000-000000000001",
            revision: 2,
            expires_at: 1790294400,
            max_streams: 4,
            services: ["cloud_applications", "rdp"],
        };
        vi.mocked(axiosHttp.get).mockResolvedValue({ data: licenseStatus } as never);

        await expect(getManagedLicenseStatus()).resolves.toEqual(licenseStatus);
        expect(axiosHttp.get).toHaveBeenCalledExactlyOnceWith("/api/console/managed/license");
    });

    it("sends a signed license only in the administrator request body", async () => {
        const licenseStatus: ManagedLicenseStatus = {
            license_id: "00000000-0000-0000-0000-000000000002",
            revision: 1,
            expires_at: 1790294400,
            max_streams: 2,
            services: ["desktop"],
        };
        vi.mocked(axiosHttp.put).mockResolvedValue({ data: licenseStatus } as never);

        await expect(installManagedLicense("signed-license-wire")).resolves.toEqual(licenseStatus);
        expect(axiosHttp.put).toHaveBeenCalledExactlyOnceWith(
            "/api/console/managed/license",
            { wire: "signed-license-wire" },
        );
    });
});
