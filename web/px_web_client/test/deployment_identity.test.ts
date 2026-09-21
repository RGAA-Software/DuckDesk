import { describe, expect, it, vi } from "vitest";
import {
    DeploymentIdentityError,
    DeploymentIdentityGate,
    type DeploymentIdentityConfiguration,
} from "../src/rtc/deployment_identity";

const CERTIFICATE_DOMAIN = new TextEncoder().encode("Pixels-Deployment-Certificate-v2\0");
const DESCRIPTOR_DOMAIN = new TextEncoder().encode("Pixels-Platform-Descriptor-v2\0");
const CHALLENGE_DOMAIN = new TextEncoder().encode("Pixels-Deployment-Challenge-v1\0");
const DEPLOYMENT_ID = "8f9cbade-f2c1-47d4-a92e-109675684b21";
const NOW = 1_900_000_000;

function ownedBuffer(bytes: Uint8Array): ArrayBuffer {
    const copy = new Uint8Array(bytes.byteLength);
    copy.set(bytes);
    return copy.buffer;
}

function encodeBase64Url(bytes: Uint8Array): string {
    return Buffer.from(bytes).toString("base64url");
}

function encodeHex(bytes: Uint8Array): string {
    return Buffer.from(bytes).toString("hex");
}

async function sha256Hex(bytes: Uint8Array): Promise<string> {
    return encodeHex(new Uint8Array(await crypto.subtle.digest("SHA-256", ownedBuffer(bytes))));
}

async function signWire(prefix: string, domain: Uint8Array, value: object, privateKey: CryptoKey): Promise<string> {
    const payload = new TextEncoder().encode(JSON.stringify(value));
    const message = new Uint8Array(domain.length + payload.length);
    message.set(domain);
    message.set(payload, domain.length);
    const signature = new Uint8Array(await crypto.subtle.sign("Ed25519", privateKey, ownedBuffer(message)));
    return `${prefix}.${encodeBase64Url(payload)}.${encodeBase64Url(signature)}`;
}

class MemoryStorage implements Storage {
    private readonly values = new Map<string, string>();

    get length(): number {
        return this.values.size;
    }

    clear(): void {
        this.values.clear();
    }

    getItem(key: string): string | null {
        return this.values.get(key) ?? null;
    }

    key(index: number): string | null {
        return Array.from(this.values.keys())[index] ?? null;
    }

    removeItem(key: string): void {
        this.values.delete(key);
    }

    setItem(key: string, value: string): void {
        this.values.set(key, value);
    }
}

async function fixture(deploymentKind: "official" | "private" = "private", descriptorRevision = 4) {
    const vendorKeys = await crypto.subtle.generateKey("Ed25519", true, ["sign", "verify"]);
    const deploymentKeys = await crypto.subtle.generateKey("Ed25519", true, ["sign", "verify"]);
    const vendorPublicKey = new Uint8Array(await crypto.subtle.exportKey("raw", vendorKeys.publicKey));
    const deploymentPublicKey = new Uint8Array(await crypto.subtle.exportKey("raw", deploymentKeys.publicKey));
    const vendorKeyId = await sha256Hex(vendorPublicKey);
    const distribution = deploymentKind === "official" ? "official" : "customer";
    const releaseNamespace = deploymentKind === "official" ? "pixels.official" : "pixels.customer";
    const certificate = {
        schema_version: 2,
        deployment_id: DEPLOYMENT_ID,
        deployment_kind: deploymentKind,
        distribution,
        release_namespace: releaseNamespace,
        oem_id: null,
        deployment_public_key_hex: encodeHex(deploymentPublicKey),
        certificate_version: 2,
        not_before: NOW - 60,
        expires_at: NOW + 86_400,
        issuer_key_id: vendorKeyId,
    };
    const descriptor = {
        schema_version: 2,
        deployment_id: DEPLOYMENT_ID,
        deployment_kind: deploymentKind,
        distribution,
        release_namespace: releaseNamespace,
        oem_id: null,
        descriptor_revision: descriptorRevision,
        trust_epoch: 3,
        issued_at: NOW - 1,
        expires_at: NOW + 300,
        minimum_client_build: 20,
        api_versions: ["console.v1", "node.v1"],
        minimum_protocol_version: 1,
        maximum_protocol_version: 1,
        authentication_methods: ["guest", "password"],
        registration_policy: "open",
        console_api_path: "/api/console",
        node_control_path: "/api/console/node-control",
    };
    const identity = {
        certificate_wire: await signWire("PXDC2", CERTIFICATE_DOMAIN, certificate, vendorKeys.privateKey),
        descriptor_wire: await signWire("PXDD2", DESCRIPTOR_DOMAIN, descriptor, deploymentKeys.privateKey),
    };
    const configuration: DeploymentIdentityConfiguration = {
        distribution,
        policy: {
            schema_version: 2,
            distribution,
            release_namespace: releaseNamespace,
            oem_id: null,
            expected_deployment_id: deploymentKind === "official" ? DEPLOYMENT_ID : null,
            official_console_origin: deploymentKind === "official" ? "https://console.example.test" : null,
            minimum_certificate_version: 2,
            minimum_descriptor_revision: 1,
            minimum_trust_epoch: 3,
            protocol_version: 1,
        },
        trustStore: {
            schema_version: 1,
            trust_epoch: 3,
            trusted_keys: [{ key_id: vendorKeyId, public_key_hex: encodeHex(vendorPublicKey) }],
        },
        clientBuild: 20,
        oemProfileSha256: null,
    };
    return { configuration, identity, deploymentKeys, descriptor };
}

async function identityFetch(identityFixture: Awaited<ReturnType<typeof fixture>>, wrongChallengeNonce = false) {
    return vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
        const url = String(input);
        if (url.endsWith("/.well-known/pixels")) return Response.json(identityFixture.identity);
        if (!url.endsWith("/.well-known/pixels/challenge") || typeof init?.body !== "string") return new Response(null, { status: 404 });
        const request = JSON.parse(init.body) as { nonce: string; descriptor_revision: number };
        const challenge = {
            schema_version: 1,
            deployment_id: DEPLOYMENT_ID,
            descriptor_revision: request.descriptor_revision,
            nonce: wrongChallengeNonce ? "A".repeat(43) : request.nonce,
            issued_at: NOW,
            expires_at: NOW + 30,
        };
        return Response.json({
            proof_wire: await signWire("PXDP1", CHALLENGE_DOMAIN, challenge, identityFixture.deploymentKeys.privateKey),
        });
    });
}

describe("Web deployment identity gate", () => {
    it("verifies a private Console and persists monotonic deployment watermarks", async () => {
        const identityFixture = await fixture();
        const requests = await identityFetch(identityFixture);
        const storage = new MemoryStorage();
        const gate = new DeploymentIdentityGate(identityFixture.configuration, requests, storage, () => NOW);

        const verified = await gate.verify("https://private.example.test");

        expect(verified?.deploymentId).toBe(DEPLOYMENT_ID);
        expect(verified?.deploymentKind).toBe("private");
        expect(requests).toHaveBeenCalledTimes(2);
        expect(storage.length).toBe(1);
        await expect(gate.verify("https://private.example.test")).resolves.toEqual(verified);
        expect(requests).toHaveBeenCalledTimes(2);

        const downgradedFixture = await fixture("private", 3);
        const downgradedRequests = await identityFetch(downgradedFixture);
        const restartedGate = new DeploymentIdentityGate(downgradedFixture.configuration, downgradedRequests, storage, () => NOW);
        await expect(restartedGate.verify("https://private.example.test")).rejects.toBeInstanceOf(DeploymentIdentityError);
        expect(downgradedRequests).toHaveBeenCalledTimes(2);
    });

    it("rejects the wrong deployment category and noncanonical Official origin", async () => {
        const privateFixture = await fixture("private");
        const officialConfiguration: DeploymentIdentityConfiguration = {
            ...privateFixture.configuration,
            distribution: "official",
            policy: {
                ...privateFixture.configuration.policy!,
                distribution: "official",
                release_namespace: "pixels.official",
                expected_deployment_id: DEPLOYMENT_ID,
                official_console_origin: "https://console.example.test",
            },
        };
        const requests = await identityFetch(privateFixture);
        const gate = new DeploymentIdentityGate(officialConfiguration, requests, new MemoryStorage(), () => NOW);

        await expect(gate.verify("https://CONSOLE.example.test")).rejects.toBeInstanceOf(DeploymentIdentityError);
        expect(requests).not.toHaveBeenCalled();
        await expect(gate.verify("https://console.example.test")).rejects.toBeInstanceOf(DeploymentIdentityError);
        expect(requests).toHaveBeenCalledTimes(1);
    });

    it("rejects a challenge that is not bound to the generated nonce", async () => {
        const identityFixture = await fixture();
        const requests = await identityFetch(identityFixture, true);
        const gate = new DeploymentIdentityGate(identityFixture.configuration, requests, new MemoryStorage(), () => NOW);

        await expect(gate.verify("https://private.example.test")).rejects.toBeInstanceOf(DeploymentIdentityError);
        expect(requests).toHaveBeenCalledTimes(2);
    });
});
