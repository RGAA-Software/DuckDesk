export const licensedServices = ["cloud_applications", "desktop", "rdp"] as const;
export type LicensedService = (typeof licensedServices)[number];
export type Terms = {
    customer_id: string;
    deployment_id: string;
    expires_at: number;
    max_streams: number;
    services: LicensedService[];
};
export type Payload = Omit<Terms, "customer_id"> & {
    license_id: string;
    revision: number;
    issued_at: number;
    key_id: string;
};
export type License = {
    license_id: string;
    customer_id: string;
    revision: number;
    revoked_at: string | null;
    wire: string;
};
export type Customer = { id: string; name: string; remark: string };
export function readPayload(wire: string): Payload {
    const parts = wire.split(".");
    if (parts.length !== 3 || parts[0] !== "PXLIC2" || wire.length > 8192)
        throw new Error("invalid display payload");
    // Display only, not cryptographic authorization. Server performs all security decisions.
    return JSON.parse(
        new TextDecoder().decode(
            Uint8Array.from(atob(parts[1]!.replace(/-/g, "+").replace(/_/g, "/")), c =>
                c.charCodeAt(0),
            ),
        ),
    ) as Payload;
}
const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
export function validTerms(value: Terms, now = Math.floor(Date.now() / 1000)): boolean {
    return (
        [value.customer_id, value.deployment_id].every(
            id => uuid.test(id) && id !== "00000000-0000-0000-0000-000000000000",
        ) &&
        Number.isSafeInteger(value.expires_at) &&
        value.expires_at > now &&
        value.expires_at <= 253402300799 &&
        Number.isInteger(value.max_streams) &&
        value.max_streams > 0 &&
        value.max_streams <= 4294967295 &&
        value.services.length > 0 &&
        value.services.length <= 3 &&
        value.services.every(
            (licensedService, serviceIndex) =>
                licensedServices.includes(licensedService) &&
                (serviceIndex === 0 || value.services[serviceIndex - 1]! < licensedService),
        )
    );
}
export function requestIdentity() {
    let body = "",
        id = "";
    return {
        next(value: unknown) {
            const next = JSON.stringify(value);
            if (next !== body) {
                body = next;
                id = crypto.randomUUID();
            }
            return id;
        },
        reset() {
            body = "";
            id = "";
        },
    };
}
