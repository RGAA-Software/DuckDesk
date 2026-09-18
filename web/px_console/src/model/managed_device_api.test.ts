import { beforeEach, describe, expect, it, vi } from "vitest";
import axiosHttp from "@/http";
import {
    createManagedDevice,
    listManagedDevices,
    replaceManagedDeviceAccess,
    rotateManagedDeviceCredential,
    type ManagedDevice,
} from "./managed_device_api";

vi.mock("@/http", () => ({
    default: {
        get: vi.fn(),
        post: vi.fn(),
        patch: vi.fn(),
        put: vi.fn(),
        delete: vi.fn(),
    },
}));

function device(index: number): ManagedDevice {
    return {
        id: `00000000-0000-0000-0000-${String(index).padStart(12, "0")}`,
        public_code: `DEVICE-${index}`,
        name: `Device ${index}`,
        platform: "windows",
        disabled: false,
        revision: 3,
        registered_at: "2026-09-18T00:00:00Z",
    };
}

describe("PostgreSQL managed device API", () => {
    beforeEach(() => vi.clearAllMocks());

    it("walks managed device UUID cursors", async () => {
        const firstPage = Array.from({ length: 100 }, (_, index) => device(index + 1));
        vi.mocked(axiosHttp.get)
            .mockResolvedValueOnce({ data: firstPage } as never)
            .mockResolvedValueOnce({ data: [device(101)] } as never);

        await expect(listManagedDevices()).resolves.toHaveLength(101);
        expect(axiosHttp.get).toHaveBeenNthCalledWith(2, "/api/console/managed/devices", {
            params: { after: firstPage.at(-1)?.id, limit: 100 },
        });
    });

    it("returns the one-time enrollment credential only from create and rotate", async () => {
        const response = { device: device(1), enrollment_token: "secret" };
        vi.mocked(axiosHttp.post).mockResolvedValue({ data: response } as never);

        await expect(createManagedDevice("Render 1", "windows")).resolves.toEqual(response);
        await expect(rotateManagedDeviceCredential(response.device)).resolves.toEqual(response);

        expect(axiosHttp.post).toHaveBeenNthCalledWith(1, "/api/console/managed/devices", {
            name: "Render 1",
            platform: "windows",
        });
        expect(axiosHttp.post).toHaveBeenNthCalledWith(
            2,
            `/api/console/managed/devices/${response.device.id}/credential`,
            {
                revision: 3,
            },
        );
    });

    it("replaces access using the device revision", async () => {
        const current = device(1);
        vi.mocked(axiosHttp.put).mockResolvedValue({ data: { ...current, revision: 4 } } as never);

        await replaceManagedDeviceAccess(current, { users: ["user-id"], groups: ["group-id"] });

        expect(axiosHttp.put).toHaveBeenCalledWith(
            `/api/console/managed/devices/${current.id}/access`,
            {
                revision: 3,
                users: ["user-id"],
                groups: ["group-id"],
            },
        );
    });
});
