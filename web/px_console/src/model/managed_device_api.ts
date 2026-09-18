import axiosHttp from "@/http";

export type DevicePlatform = "windows" | "linux" | "macos" | "android";

export interface ManagedDevice {
    id: string;
    public_code: string;
    name: string;
    platform: DevicePlatform;
    disabled: boolean;
    revision: number;
    registered_at: string;
}

export interface DeviceAccess {
    users: string[];
    groups: string[];
}

interface DeviceCredentialResponse {
    device: ManagedDevice;
    enrollment_token: string;
}

export async function listManagedDevices(): Promise<ManagedDevice[]> {
    const devices: ManagedDevice[] = [];
    let after: string | undefined;
    for (;;) {
        const response = await axiosHttp.get<ManagedDevice[]>("/api/console/managed/devices", {
            params: { after, limit: 100 },
        });
        devices.push(...response.data);
        if (response.data.length < 100) return devices;
        after = response.data.at(-1)?.id;
        if (!after) throw new Error("A managed device page did not include its cursor identity");
    }
}

export async function createManagedDevice(
    name: string,
    platform: DevicePlatform,
): Promise<DeviceCredentialResponse> {
    const response = await axiosHttp.post<DeviceCredentialResponse>(
        "/api/console/managed/devices",
        { name, platform },
    );
    return response.data;
}

export async function updateManagedDevice(
    device: ManagedDevice,
    name: string,
    disabled: boolean,
): Promise<ManagedDevice> {
    const response = await axiosHttp.patch<ManagedDevice>(
        `/api/console/managed/devices/${encodeURIComponent(device.id)}`,
        {
            revision: device.revision,
            name,
            disabled,
        },
    );
    return response.data;
}

export async function deleteManagedDevice(device: ManagedDevice): Promise<void> {
    await axiosHttp.delete(`/api/console/managed/devices/${encodeURIComponent(device.id)}`, {
        params: { revision: device.revision },
    });
}

export async function rotateManagedDeviceCredential(
    device: ManagedDevice,
): Promise<DeviceCredentialResponse> {
    const response = await axiosHttp.post<DeviceCredentialResponse>(
        `/api/console/managed/devices/${encodeURIComponent(device.id)}/credential`,
        { revision: device.revision },
    );
    return response.data;
}

export async function getManagedDeviceAccess(device: ManagedDevice): Promise<DeviceAccess> {
    const response = await axiosHttp.get<DeviceAccess>(
        `/api/console/managed/devices/${encodeURIComponent(device.id)}/access`,
    );
    return response.data;
}

export async function replaceManagedDeviceAccess(
    device: ManagedDevice,
    access: DeviceAccess,
): Promise<ManagedDevice> {
    const response = await axiosHttp.put<ManagedDevice>(
        `/api/console/managed/devices/${encodeURIComponent(device.id)}/access`,
        {
            revision: device.revision,
            users: access.users,
            groups: access.groups,
        },
    );
    return response.data;
}
