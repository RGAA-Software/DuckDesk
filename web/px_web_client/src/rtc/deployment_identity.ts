export type WebDistribution = "development" | "official" | "customer";

export interface DeploymentPolicy {
    schema_version: number;
    distribution: "official" | "customer";
    expected_deployment_id: string | null;
    official_console_origin: string | null;
    minimum_certificate_version: number;
    minimum_descriptor_revision: number;
    minimum_trust_epoch: number;
    protocol_version: number;
}

export interface DeploymentTrustStore {
    schema_version: number;
    trust_epoch: number;
    trusted_keys: Array<{ key_id: string; public_key_hex: string }>;
}

export interface DeploymentIdentityConfiguration {
    distribution: WebDistribution;
    policy: DeploymentPolicy | null;
    trustStore: DeploymentTrustStore | null;
    clientBuild: number;
}

export interface VerifiedDeploymentIdentity {
    deploymentId: string;
    deploymentKind: "official" | "private";
    certificateVersion: number;
    descriptorRevision: number;
    trustEpoch: number;
    descriptorExpiresAt: number;
}

interface DeploymentCertificate {
    schema_version: number;
    deployment_id: string;
    deployment_kind: "official" | "private";
    deployment_public_key_hex: string;
    certificate_version: number;
    not_before: number;
    expires_at: number;
    issuer_key_id: string;
}

interface PlatformDescriptor {
    schema_version: number;
    deployment_id: string;
    deployment_kind: "official" | "private";
    descriptor_revision: number;
    trust_epoch: number;
    issued_at: number;
    expires_at: number;
    minimum_client_build: number;
    api_versions: string[];
    minimum_protocol_version: number;
    maximum_protocol_version: number;
    authentication_methods: string[];
    registration_policy: "closed" | "open";
    console_api_path: string;
    node_control_path: string;
}

interface ChallengePayload {
    schema_version: number;
    deployment_id: string;
    descriptor_revision: number;
    nonce: string;
    issued_at: number;
    expires_at: number;
}

interface Watermark {
    schema_version: number;
    deployment_id: string;
    deployment_kind: "official" | "private";
    certificate_version: number;
    descriptor_revision: number;
    trust_epoch: number;
}

interface FetchResponse {
    ok: boolean;
    headers: Headers;
    body: ReadableStream<Uint8Array> | null;
}

type IdentityFetch = (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>;

const MAX_RESPONSE_BYTES = 64 * 1024;
const CERTIFICATE_DOMAIN = new TextEncoder().encode("Pixels-Deployment-Certificate-v1\0");
const DESCRIPTOR_DOMAIN = new TextEncoder().encode("Pixels-Platform-Descriptor-v1\0");
const CHALLENGE_DOMAIN = new TextEncoder().encode("Pixels-Deployment-Challenge-v1\0");
const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const HEX_32_PATTERN = /^[0-9a-f]{64}$/;

export class DeploymentIdentityError extends Error {
    constructor() {
        super("DEPLOYMENT_IDENTITY_REJECTED");
        this.name = "DeploymentIdentityError";
    }
}

function reject(): never {
    throw new DeploymentIdentityError();
}

function exactFields(value: unknown, expected: readonly string[]): value is Record<string, unknown> {
    return typeof value === "object" && value !== null && !Array.isArray(value) &&
        JSON.stringify(Object.keys(value)) === JSON.stringify(expected);
}

function positiveSafeInteger(value: unknown): value is number {
    return Number.isSafeInteger(value) && Number(value) > 0;
}

function unixTime(value: unknown): value is number {
    return Number.isSafeInteger(value) && Number(value) >= 0;
}

function canonicalUuid(value: unknown): value is string {
    return typeof value === "string" && UUID_PATTERN.test(value) && value !== "00000000-0000-0000-0000-000000000000";
}

function canonicalHttpsOrigin(value: string): string {
    let parsed: URL;
    try {
        parsed = new URL(value);
    } catch {
        reject();
    }
    if (parsed.protocol !== "https:" || parsed.origin !== value || parsed.username || parsed.password || parsed.pathname !== "/" ||
        parsed.search || parsed.hash) {
        reject();
    }
    return value;
}

function decodeHex(value: string): Uint8Array {
    if (!HEX_32_PATTERN.test(value)) reject();
    const bytes = new Uint8Array(32);
    for (let index = 0; index < bytes.length; index += 1) {
        bytes[index] = Number.parseInt(value.slice(index * 2, index * 2 + 2), 16);
    }
    if (bytes.every(byte => byte === 0)) reject();
    return bytes;
}

function encodeHex(bytes: Uint8Array): string {
    return Array.from(bytes, byte => byte.toString(16).padStart(2, "0")).join("");
}

function decodeBase64Url(value: string): Uint8Array {
    if (!/^[A-Za-z0-9_-]+$/.test(value) || value.length % 4 === 1) reject();
    const padded = value.replace(/-/g, "+").replace(/_/g, "/") + "=".repeat((4 - value.length % 4) % 4);
    let binary: string;
    try {
        binary = atob(padded);
    } catch {
        reject();
    }
    const bytes = Uint8Array.from(binary, character => character.charCodeAt(0));
    if (encodeBase64Url(bytes) !== value) reject();
    return bytes;
}

function encodeBase64Url(bytes: Uint8Array): string {
    let binary = "";
    for (const byte of bytes) binary += String.fromCharCode(byte);
    return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

function concatenate(first: Uint8Array, second: Uint8Array): Uint8Array {
    const result = new Uint8Array(first.length + second.length);
    result.set(first);
    result.set(second, first.length);
    return result;
}

function ownedBuffer(bytes: Uint8Array): ArrayBuffer {
    const copy = new Uint8Array(bytes.byteLength);
    copy.set(bytes);
    return copy.buffer;
}

async function verifySignature(publicKey: Uint8Array, domain: Uint8Array, payload: Uint8Array, signature: Uint8Array): Promise<void> {
    let verificationKey: CryptoKey;
    try {
        verificationKey = await crypto.subtle.importKey("raw", ownedBuffer(publicKey), { name: "Ed25519" }, false, ["verify"]);
        if (!await crypto.subtle.verify("Ed25519", verificationKey, ownedBuffer(signature), ownedBuffer(concatenate(domain, payload)))) reject();
    } catch (error) {
        if (error instanceof DeploymentIdentityError) throw error;
        reject();
    }
}

function decodeWire(wire: string, prefix: string): { payload: Uint8Array; signature: Uint8Array; value: unknown } {
    if (!wire || wire.length > 16 * 1024) reject();
    const parts = wire.split(".");
    if (parts.length !== 3 || parts[0] !== prefix) reject();
    const payload = decodeBase64Url(parts[1]);
    const signature = decodeBase64Url(parts[2]);
    if (signature.length !== 64) reject();
    let payloadText: string;
    let value: unknown;
    try {
        payloadText = new TextDecoder("utf-8", { fatal: true }).decode(payload);
        value = JSON.parse(payloadText);
    } catch {
        reject();
    }
    if (JSON.stringify(value) !== payloadText) reject();
    return { payload, signature, value };
}

async function sha256Hex(bytes: Uint8Array): Promise<string> {
    return encodeHex(new Uint8Array(await crypto.subtle.digest("SHA-256", ownedBuffer(bytes))));
}

function parseCertificate(value: unknown): DeploymentCertificate {
    const fields = ["schema_version", "deployment_id", "deployment_kind", "deployment_public_key_hex", "certificate_version", "not_before",
        "expires_at", "issuer_key_id"];
    if (!exactFields(value, fields) || value.schema_version !== 1 || !canonicalUuid(value.deployment_id) ||
        (value.deployment_kind !== "official" && value.deployment_kind !== "private") ||
        typeof value.deployment_public_key_hex !== "string" || !positiveSafeInteger(value.certificate_version) || !unixTime(value.not_before) ||
        !unixTime(value.expires_at) || value.expires_at <= value.not_before || value.expires_at > 253_402_300_799 ||
        typeof value.issuer_key_id !== "string" || !HEX_32_PATTERN.test(value.issuer_key_id)) {
        reject();
    }
    decodeHex(value.deployment_public_key_hex);
    return value as unknown as DeploymentCertificate;
}

function sortedUniqueTokens(value: unknown, maximum: number): value is string[] {
    if (!Array.isArray(value) || value.length === 0 || value.length > maximum ||
        value.some(token => typeof token !== "string" || !/^[a-z0-9._-]{1,32}$/.test(token))) return false;
    return value.every((token, index) => index === 0 || value[index - 1] < token);
}

function parseDescriptor(value: unknown): PlatformDescriptor {
    const fields = ["schema_version", "deployment_id", "deployment_kind", "descriptor_revision", "trust_epoch", "issued_at", "expires_at",
        "minimum_client_build", "api_versions", "minimum_protocol_version", "maximum_protocol_version", "authentication_methods",
        "registration_policy", "console_api_path", "node_control_path"];
    if (!exactFields(value, fields) || value.schema_version !== 1 || !canonicalUuid(value.deployment_id) ||
        (value.deployment_kind !== "official" && value.deployment_kind !== "private") || !positiveSafeInteger(value.descriptor_revision) ||
        !positiveSafeInteger(value.trust_epoch) || !unixTime(value.issued_at) || !unixTime(value.expires_at) || value.expires_at <= value.issued_at ||
        value.expires_at - value.issued_at > 86_400 || !positiveSafeInteger(value.minimum_client_build) ||
        !sortedUniqueTokens(value.api_versions, 16) || !positiveSafeInteger(value.minimum_protocol_version) ||
        !positiveSafeInteger(value.maximum_protocol_version) || value.maximum_protocol_version < value.minimum_protocol_version ||
        !sortedUniqueTokens(value.authentication_methods, 8) || (value.registration_policy !== "closed" && value.registration_policy !== "open") ||
        value.console_api_path !== "/api/console" || value.node_control_path !== "/api/console/node-control") {
        reject();
    }
    return value as unknown as PlatformDescriptor;
}

function parseChallenge(value: unknown): ChallengePayload {
    const fields = ["schema_version", "deployment_id", "descriptor_revision", "nonce", "issued_at", "expires_at"];
    if (!exactFields(value, fields) || value.schema_version !== 1 || !canonicalUuid(value.deployment_id) ||
        !positiveSafeInteger(value.descriptor_revision) || typeof value.nonce !== "string" || decodeBase64Url(value.nonce).length !== 32 ||
        !unixTime(value.issued_at) || !unixTime(value.expires_at) || value.expires_at <= value.issued_at || value.expires_at - value.issued_at > 60) {
        reject();
    }
    return value as unknown as ChallengePayload;
}

function parsePolicy(text: string, distribution: WebDistribution): DeploymentPolicy | null {
    if (distribution === "development") return null;
    let value: unknown;
    try {
        value = JSON.parse(text);
    } catch {
        reject();
    }
    const fields = ["schema_version", "distribution", "expected_deployment_id", "official_console_origin", "minimum_certificate_version",
        "minimum_descriptor_revision", "minimum_trust_epoch", "protocol_version"];
    if (!exactFields(value, fields) || value.schema_version !== 1 || value.distribution !== distribution ||
        !positiveSafeInteger(value.minimum_certificate_version) || !positiveSafeInteger(value.minimum_descriptor_revision) ||
        !positiveSafeInteger(value.minimum_trust_epoch) || !positiveSafeInteger(value.protocol_version)) {
        reject();
    }
    if (distribution === "official") {
        if (!canonicalUuid(value.expected_deployment_id) || typeof value.official_console_origin !== "string") reject();
        canonicalHttpsOrigin(value.official_console_origin);
    } else if (value.expected_deployment_id !== null || value.official_console_origin !== null) {
        reject();
    }
    return value as unknown as DeploymentPolicy;
}

async function parseTrustStore(text: string, requiredEpoch: number): Promise<DeploymentTrustStore> {
    let value: unknown;
    try {
        value = JSON.parse(text);
    } catch {
        reject();
    }
    if (!exactFields(value, ["schema_version", "trust_epoch", "trusted_keys"]) || value.schema_version !== 1 ||
        !positiveSafeInteger(value.trust_epoch) || value.trust_epoch !== requiredEpoch || !Array.isArray(value.trusted_keys) ||
        value.trusted_keys.length < 1 || value.trusted_keys.length > 16 || JSON.stringify(value) !== text) {
        reject();
    }
    let previousKeyId = "";
    for (const trustedKey of value.trusted_keys) {
        if (!exactFields(trustedKey, ["key_id", "public_key_hex"]) || typeof trustedKey.key_id !== "string" ||
            typeof trustedKey.public_key_hex !== "string" || !HEX_32_PATTERN.test(trustedKey.key_id) || trustedKey.key_id <= previousKeyId) {
            reject();
        }
        const publicKey = decodeHex(trustedKey.public_key_hex);
        if (await sha256Hex(publicKey) !== trustedKey.key_id) reject();
        previousKeyId = trustedKey.key_id;
    }
    return value as unknown as DeploymentTrustStore;
}

async function readBoundedJson(response: FetchResponse): Promise<unknown> {
    if (!response.ok) reject();
    const contentLength = response.headers.get("content-length");
    if (contentLength !== null && (!/^[0-9]+$/.test(contentLength) || Number(contentLength) > MAX_RESPONSE_BYTES)) reject();
    if (!response.body) reject();
    const reader = response.body.getReader();
    const chunks: Uint8Array[] = [];
    let total = 0;
    while (true) {
        const read = await reader.read();
        if (read.done) break;
        total += read.value.byteLength;
        if (total > MAX_RESPONSE_BYTES) {
            await reader.cancel();
            reject();
        }
        chunks.push(read.value);
    }
    const bytes = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) {
        bytes.set(chunk, offset);
        offset += chunk.byteLength;
    }
    try {
        return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
    } catch {
        reject();
    }
}

function parseWatermark(text: string): Watermark {
    let value: unknown;
    try {
        value = JSON.parse(text);
    } catch {
        reject();
    }
    const fields = ["schema_version", "deployment_id", "deployment_kind", "certificate_version", "descriptor_revision", "trust_epoch"];
    if (!exactFields(value, fields) || value.schema_version !== 1 || !canonicalUuid(value.deployment_id) ||
        (value.deployment_kind !== "official" && value.deployment_kind !== "private") || !positiveSafeInteger(value.certificate_version) ||
        !positiveSafeInteger(value.descriptor_revision) || !positiveSafeInteger(value.trust_epoch)) {
        reject();
    }
    return value as unknown as Watermark;
}

export function packagedDeploymentIdentityConfiguration(): DeploymentIdentityConfiguration {
    const distribution = __PIXELS_WEB_DISTRIBUTION__;
    if (distribution === "development") return { distribution, policy: null, trustStore: null, clientBuild: 0 };
    const policy = parsePolicy(__PIXELS_WEB_DEPLOYMENT_POLICY__, distribution);
    if (!policy) reject();
    const trustStore = JSON.parse(__PIXELS_WEB_DEPLOYMENT_TRUST__) as DeploymentTrustStore;
    return { distribution, policy, trustStore, clientBuild: __PIXELS_WEB_CLIENT_BUILD__ };
}

export class DeploymentIdentityGate {
    private readonly configuration: DeploymentIdentityConfiguration;
    private readonly fetchIdentity: IdentityFetch;
    private readonly storage: Storage | null;
    private readonly now: () => number;
    private validatedTrustStore: Promise<DeploymentTrustStore> | null = null;
    private readonly cache = new Map<string, { identity: VerifiedDeploymentIdentity; validUntil: number }>();

    constructor(configuration: DeploymentIdentityConfiguration, fetchIdentity: IdentityFetch = fetch,
        storage: Storage | null = typeof localStorage === "undefined" ? null : localStorage,
        now: () => number = () => Math.floor(Date.now() / 1000)) {
        this.configuration = configuration;
        this.fetchIdentity = fetchIdentity;
        this.storage = storage;
        this.now = now;
    }

    requiresVerifiedConsole(): boolean {
        return this.configuration.distribution !== "development";
    }

    async verify(consoleOrigin: string): Promise<VerifiedDeploymentIdentity | null> {
        if (!this.requiresVerifiedConsole()) return null;
        const policy = this.configuration.policy;
        if (!policy || this.configuration.clientBuild <= 0) reject();
        const canonicalOrigin = canonicalHttpsOrigin(consoleOrigin);
        if (this.configuration.distribution === "official" && canonicalOrigin !== policy.official_console_origin) reject();
        const now = this.now();
        const cached = this.cache.get(canonicalOrigin);
        if (cached && cached.validUntil > now) return cached.identity;
        const trustStore = await this.trustStore(policy.minimum_trust_epoch);
        const watermark = this.readWatermark(canonicalOrigin);
        const identityResponse = await this.fetchIdentity(`${canonicalOrigin}/.well-known/pixels`, {
            method: "GET",
            credentials: "omit",
            redirect: "error",
            cache: "no-store",
        });
        const identityJson = await readBoundedJson(identityResponse);
        if (!exactFields(identityJson, ["certificate_wire", "descriptor_wire"]) || typeof identityJson.certificate_wire !== "string" ||
            typeof identityJson.descriptor_wire !== "string") reject();
        const certificateWire = decodeWire(identityJson.certificate_wire, "PXDC1");
        const certificate = parseCertificate(certificateWire.value);
        const trustedKey = trustStore.trusted_keys.find(key => key.key_id === certificate.issuer_key_id);
        if (!trustedKey) reject();
        await verifySignature(decodeHex(trustedKey.public_key_hex), CERTIFICATE_DOMAIN, certificateWire.payload, certificateWire.signature);
        const expectedKind = this.configuration.distribution === "official" ? "official" : "private";
        const expectedDeploymentId = policy.expected_deployment_id ?? watermark?.deployment_id ?? null;
        if (certificate.deployment_kind !== expectedKind || (expectedDeploymentId !== null && certificate.deployment_id !== expectedDeploymentId) ||
            certificate.certificate_version < policy.minimum_certificate_version || certificate.not_before > now || certificate.expires_at <= now) reject();

        const descriptorWire = decodeWire(identityJson.descriptor_wire, "PXDD1");
        await verifySignature(decodeHex(certificate.deployment_public_key_hex), DESCRIPTOR_DOMAIN, descriptorWire.payload, descriptorWire.signature);
        const descriptor = parseDescriptor(descriptorWire.value);
        if (descriptor.deployment_id !== certificate.deployment_id || descriptor.deployment_kind !== certificate.deployment_kind ||
            descriptor.descriptor_revision < policy.minimum_descriptor_revision || descriptor.trust_epoch < policy.minimum_trust_epoch ||
            descriptor.issued_at > now || descriptor.expires_at <= now || descriptor.minimum_client_build > this.configuration.clientBuild ||
            policy.protocol_version < descriptor.minimum_protocol_version || policy.protocol_version > descriptor.maximum_protocol_version) reject();

        const nonceBytes = new Uint8Array(32);
        crypto.getRandomValues(nonceBytes);
        const nonce = encodeBase64Url(nonceBytes);
        const challengeResponse = await this.fetchIdentity(`${canonicalOrigin}/.well-known/pixels/challenge`, {
            method: "POST",
            credentials: "omit",
            redirect: "error",
            cache: "no-store",
            headers: { "content-type": "application/json" },
            body: JSON.stringify({ nonce, descriptor_revision: descriptor.descriptor_revision }),
        });
        const challengeJson = await readBoundedJson(challengeResponse);
        if (!exactFields(challengeJson, ["proof_wire"]) || typeof challengeJson.proof_wire !== "string") reject();
        const challengeWire = decodeWire(challengeJson.proof_wire, "PXDP1");
        await verifySignature(decodeHex(certificate.deployment_public_key_hex), CHALLENGE_DOMAIN, challengeWire.payload, challengeWire.signature);
        const challenge = parseChallenge(challengeWire.value);
        if (challenge.deployment_id !== certificate.deployment_id || challenge.descriptor_revision !== descriptor.descriptor_revision ||
            challenge.nonce !== nonce || challenge.issued_at > now || challenge.expires_at <= now) reject();

        const verified: VerifiedDeploymentIdentity = {
            deploymentId: certificate.deployment_id,
            deploymentKind: certificate.deployment_kind,
            certificateVersion: certificate.certificate_version,
            descriptorRevision: descriptor.descriptor_revision,
            trustEpoch: descriptor.trust_epoch,
            descriptorExpiresAt: descriptor.expires_at,
        };
        this.writeWatermark(canonicalOrigin, verified, watermark);
        this.cache.set(canonicalOrigin, { identity: verified, validUntil: Math.min(now + 15, descriptor.expires_at) });
        return verified;
    }

    private async trustStore(requiredEpoch: number): Promise<DeploymentTrustStore> {
        if (!this.validatedTrustStore) {
            const serialized = JSON.stringify(this.configuration.trustStore);
            this.validatedTrustStore = parseTrustStore(serialized, requiredEpoch);
        }
        return this.validatedTrustStore;
    }

    private watermarkKey(consoleOrigin: string): string {
        return `pixels.web.deployment.${consoleOrigin}`;
    }

    private readWatermark(consoleOrigin: string): Watermark | null {
        if (!this.storage) reject();
        let serialized: string | null;
        try {
            serialized = this.storage.getItem(this.watermarkKey(consoleOrigin));
        } catch {
            reject();
        }
        return serialized === null ? null : parseWatermark(serialized);
    }

    private writeWatermark(consoleOrigin: string, identity: VerifiedDeploymentIdentity, previous: Watermark | null): void {
        if (!this.storage) reject();
        if (previous && (previous.deployment_id !== identity.deploymentId || previous.deployment_kind !== identity.deploymentKind ||
            previous.certificate_version > identity.certificateVersion || previous.descriptor_revision > identity.descriptorRevision ||
            previous.trust_epoch > identity.trustEpoch)) reject();
        const watermark: Watermark = {
            schema_version: 1,
            deployment_id: identity.deploymentId,
            deployment_kind: identity.deploymentKind,
            certificate_version: identity.certificateVersion,
            descriptor_revision: identity.descriptorRevision,
            trust_epoch: identity.trustEpoch,
        };
        try {
            this.storage.setItem(this.watermarkKey(consoleOrigin), JSON.stringify(watermark));
        } catch {
            reject();
        }
    }
}

export const packagedDeploymentIdentityGate = new DeploymentIdentityGate(packagedDeploymentIdentityConfiguration());
