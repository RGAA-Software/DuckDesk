export const products = ["pixels_console", "gopico", "clientbox", "goagent"] as const;
export const capabilities = ["cloud_applications", "desktop", "rdp"] as const;
export type Product = (typeof products)[number];
export type Capability = (typeof capabilities)[number];
export type Terms = {
    customer_id: string;
    deployment_id: string;
    product: Product;
    distribution: "official" | "customer";
    machine_sha256: string;
    mode: "trial" | "licensed";
    activation: { kind: "immediately" };
    expires_at: number;
    max_devices: number;
    max_sessions: number;
    features: Capability[];
};
export type Payload = Omit<Terms, "customer_id" | "activation"> & {
    license_id: string;
    revision: number;
    not_before: number;
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
    if (parts.length !== 3 || parts[0] !== "PXLIC1" || wire.length > 8192)
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
        /^[a-f0-9]{64}$/.test(value.machine_sha256) &&
        products.includes(value.product) &&
        ["official", "customer"].includes(value.distribution) &&
        ["trial", "licensed"].includes(value.mode) &&
        Number.isSafeInteger(value.expires_at) &&
        value.expires_at > now &&
        value.expires_at <= 253402300799 &&
        [value.max_devices, value.max_sessions].every(
            n => Number.isInteger(n) && n > 0 && n <= 4294967295,
        ) &&
        value.features.length > 0 &&
        value.features.length <= 3 &&
        value.features.every(
            (f, i) => capabilities.includes(f) && (i === 0 || value.features[i - 1]! < f),
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
