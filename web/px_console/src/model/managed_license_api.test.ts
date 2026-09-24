import { beforeEach, describe, expect, it, vi } from "vitest";
import axiosHttp from "@/http";
import { getManagedLicenseStatus, type ManagedLicenseStatus } from "./managed_license_api";

vi.mock("@/http", () => ({
    default: { get: vi.fn() },
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
});
