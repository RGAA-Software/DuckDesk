// Disposable Console process + actual built Vue application + isolated PostgreSQL database.
const assert = require("node:assert/strict");
const { execFileSync, spawn } = require("node:child_process");
const { createHash, createPrivateKey, generateKeyPairSync, randomUUID, sign } = require("node:crypto");
const { once } = require("node:events");
const fs = require("node:fs");
const net = require("node:net");
const os = require("node:os");
const path = require("node:path");
const { createRequire } = require("node:module");

const repository = path.resolve(__dirname, "../..");
const { chromium } = createRequire(path.join(repository, "web/px_pixels/package.json"))("@playwright/test");
const consoleExecutable = process.argv[2];
const administratorExecutable = process.argv[3];
const databaseExecutable = process.argv[4];
const container = process.env.PIXELS_TEST_CONTAINER;
const databaseName = "pixels_console_browser_windows";
const templateDatabaseName = "pixels_console_browser_template_windows";
const initialUsername = "browser-admin";
const initialPassword = "browser-console-password";
const createdUserPassword = "browser-created-password";

assert.equal(process.env.PIXELS_PG_ISOLATED_TEST, "1");
assert.equal(process.platform, "win32");
assert.match(container || "", /^[a-f0-9]{12,64}$/);

const docker = (...arguments) => execFileSync("docker", arguments, { encoding: "utf8", timeout: 45000, windowsHide: true });
const project = docker("inspect", container, "--format", '{{index .Config.Labels "com.docker.compose.project"}}').trim();
assert.match(project, /^pixels-pg-\d{8}-\d{6}-[a-f0-9]{8}$/);

const privateDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "pixels-console-browser-"));
const guestSourcePath = path.join(privateDirectory, "guest-source.key");
const workspaceKeyPath = path.join(privateDirectory, "workspace.key");
const passwordPath = path.join(privateDirectory, "initial-password.txt");
const recordingCachePath = path.join(privateDirectory, "recording-cache");
const licenseStatePath = path.join(privateDirectory, "license-state");
const licenseTrustPath = path.join(privateDirectory, "license-trust.json");
const licensePath = path.join(privateDirectory, "console.license");
const deploymentSigningKeyPath = path.join(privateDirectory, "deployment-signing.pk8");
const deploymentCertificatePath = path.join(privateDirectory, "deployment.certificate");
const deploymentTrustPath = path.join(privateDirectory, "deployment-trust.json");
const workspaceKeyId = randomUUID();
const licenseAuthorityDeploymentId = randomUUID();
let child;
let browser;
let baseUrl;
let serverPort;
let serverOutput = "";
let containerStopped = false;
let databaseCreated = false;

function databaseUrl(source, role, database) {
  const sourceUrl = new URL(source);
  sourceUrl.pathname = `/${database}`;
  sourceUrl.username = `pixels_console_${role}`;
  return sourceUrl.toString();
}

const ownerDatabaseUrl = databaseUrl(process.env.PIXELS_TEST_CONSOLE_OWNER_URL, "owner", databaseName);
const runtimeDatabaseUrl = databaseUrl(process.env.PIXELS_TEST_CONSOLE_RUNTIME_URL, "runtime", databaseName);

function runExecutable(executable, arguments, environment) {
  return execFileSync(executable, arguments, {
    encoding: "utf8",
    env: { ...process.env, ...environment },
    timeout: 120000,
    windowsHide: true,
  });
}

function rawEd25519PublicKey(publicKey) {
  const subjectPublicKeyInfo = publicKey.export({ format: "der", type: "spki" });
  assert.equal(subjectPublicKeyInfo.length, 44);
  return subjectPublicKeyInfo.subarray(subjectPublicKeyInfo.length - 32);
}

function keyId(publicKeyBytes) {
  return createHash("sha256").update(publicKeyBytes).digest("hex");
}

function ringEd25519PrivateKey(privateKey) {
  const privateKeyJwk = privateKey.export({ format: "jwk" });
  assert.equal(privateKeyJwk.crv, "Ed25519");
  const seed = Buffer.from(privateKeyJwk.d, "base64url");
  const publicKey = Buffer.from(privateKeyJwk.x, "base64url");
  assert.equal(seed.length, 32);
  assert.equal(publicKey.length, 32);
  return Buffer.concat([
    Buffer.from("3053020101300506032b657004220420", "hex"),
    seed,
    Buffer.from("a123032100", "hex"),
    publicKey,
  ]);
}

function signedWire(prefix, domain, payload, privateKey) {
  const canonicalPayload = Buffer.from(JSON.stringify(payload));
  const signature = sign(null, Buffer.concat([Buffer.from(domain), canonicalPayload]), privateKey);
  return `${prefix}.${canonicalPayload.toString("base64url")}.${signature.toString("base64url")}`;
}

function provisionLicenseAndDeploymentIdentity() {
  const now = Math.floor(Date.now() / 1000);
  const licenseKeyDocument = Buffer.from(
    "302e020100300506032b6570042204209d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
    "hex",
  );
  const licensePrivateKey = createPrivateKey({ key: licenseKeyDocument, format: "der", type: "pkcs8" });
  const licensePublicKey = Buffer.from("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "hex");
  const licenseKeyId = keyId(licensePublicKey);
  fs.mkdirSync(licenseStatePath);
  fs.writeFileSync(
    licenseTrustPath,
    JSON.stringify({
      schema_version: 1,
      authority_deployment_id: licenseAuthorityDeploymentId,
      recovery_generation: randomUUID(),
      active_key_id: licenseKeyId,
      trusted_keys: [{ key_id: licenseKeyId, public_key_hex: licensePublicKey.toString("hex") }],
    }),
    { flag: "wx" },
  );
  const licensePayload = {
    schema: 1,
    license_id: randomUUID(),
    deployment_id: process.env.PIXELS_DEPLOYMENT_ID,
    product: "pixels_console",
    distribution: "customer",
    machine_sha256: "a".repeat(64),
    revision: 1,
    mode: "licensed",
    issued_at: now - 10,
    not_before: now - 10,
    expires_at: now + 3600,
    max_devices: 32,
    max_sessions: 32,
    features: ["cloud_applications", "desktop", "rdp"],
    key_id: licenseKeyId,
  };
  fs.writeFileSync(
    licensePath,
    signedWire("PXLIC1", "Pixels-License-v1\0", licensePayload, licensePrivateKey),
    { flag: "wx" },
  );

  const vendorKeys = generateKeyPairSync("ed25519");
  const deploymentKeys = generateKeyPairSync("ed25519");
  const vendorPublicKey = rawEd25519PublicKey(vendorKeys.publicKey);
  const deploymentPublicKey = rawEd25519PublicKey(deploymentKeys.publicKey);
  const vendorKeyId = keyId(vendorPublicKey);
  fs.writeFileSync(
    deploymentSigningKeyPath,
    ringEd25519PrivateKey(deploymentKeys.privateKey),
    { flag: "wx" },
  );
  fs.writeFileSync(
    deploymentTrustPath,
    JSON.stringify({
      schema_version: 1,
      trust_epoch: 1,
      trusted_keys: [{ key_id: vendorKeyId, public_key_hex: vendorPublicKey.toString("hex") }],
    }),
    { flag: "wx" },
  );
  const deploymentCertificate = {
    schema_version: 1,
    deployment_id: process.env.PIXELS_DEPLOYMENT_ID,
    deployment_kind: "private",
    deployment_public_key_hex: deploymentPublicKey.toString("hex"),
    certificate_version: 1,
    not_before: now - 60,
    expires_at: now + 3600,
    issuer_key_id: vendorKeyId,
  };
  fs.writeFileSync(
    deploymentCertificatePath,
    signedWire(
      "PXDC1",
      "Pixels-Deployment-Certificate-v1\0",
      deploymentCertificate,
      vendorKeys.privateKey,
    ),
    { flag: "wx" },
  );
}

function createBrowserDatabase() {
  docker(
    "exec",
    container,
    "createdb",
    "-U",
    "pixels_admin",
    "-O",
    "pixels_console_owner",
    "-T",
    templateDatabaseName,
    databaseName,
  );
  databaseCreated = true;
  docker(
    "exec",
    container,
    "psql",
    "-X",
    "-v",
    "ON_ERROR_STOP=1",
    "-U",
    "pixels_admin",
    "-d",
    databaseName,
    "-c",
    `REVOKE ALL ON DATABASE ${databaseName} FROM PUBLIC; GRANT CONNECT ON DATABASE ${databaseName} TO pixels_console_owner,pixels_console_runtime; REVOKE CREATE ON SCHEMA public FROM PUBLIC`,
  );
  runExecutable(databaseExecutable, ["check", "console"], {
    PIXELS_DATABASE_URL: runtimeDatabaseUrl,
    PIXELS_DEPLOYMENT_ID: process.env.PIXELS_DEPLOYMENT_ID,
    PIXELS_PG_LOCAL_DEVELOPMENT: "1",
  });
}

function provisionConsole() {
  const identity = runExecutable("whoami", [], {}).trim();
  runExecutable("icacls", [privateDirectory, "/inheritance:r", "/grant:r", `${identity}:(OI)(CI)F`, "*S-1-5-18:(OI)(CI)F"], {});
  fs.mkdirSync(recordingCachePath);
  provisionLicenseAndDeploymentIdentity();
  fs.writeFileSync(passwordPath, initialPassword, { flag: "wx" });
  runExecutable(administratorExecutable, ["generate-secrets"], {
    PIXELS_CONSOLE_GUEST_SOURCE_KEY: guestSourcePath,
    PIXELS_CONSOLE_WORKSPACE_KEY: workspaceKeyPath,
    PIXELS_CONSOLE_WORKSPACE_KEY_ID: workspaceKeyId,
  });
  runExecutable(administratorExecutable, ["bootstrap"], {
    PIXELS_CONSOLE_LOCAL_DEVELOPMENT: "1",
    PIXELS_DATABASE_URL: ownerDatabaseUrl,
    PIXELS_DEPLOYMENT_ID: process.env.PIXELS_DEPLOYMENT_ID,
    PIXELS_CONSOLE_INITIAL_USERNAME: initialUsername,
    PIXELS_CONSOLE_INITIAL_PASSWORD_FILE: passwordPath,
  });
  runExecutable(administratorExecutable, ["initialize-recording-cache"], {
    PIXELS_DEPLOYMENT_ID: process.env.PIXELS_DEPLOYMENT_ID,
    PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY: recordingCachePath,
  });
}

async function reservePort() {
  const listener = net.createServer();
  await new Promise((resolve, reject) => {
    listener.once("error", reject);
    listener.listen(0, "127.0.0.1", resolve);
  });
  const address = listener.address();
  assert.equal(typeof address, "object");
  const port = address.port;
  await new Promise((resolve, reject) => listener.close((error) => (error ? reject(error) : resolve())));
  return port;
}

function timeLimit(promise, milliseconds, message) {
  return Promise.race([
    promise,
    new Promise((_, reject) => {
      const timer = setTimeout(() => reject(new Error(message)), milliseconds);
      timer.unref();
    }),
  ]);
}

async function stopServer() {
  if (child && child.exitCode === null && child.signalCode === null) {
    const exited = once(child, "exit");
    child.kill();
    await timeLimit(exited, 10000, "Console shutdown timeout");
  }
  child = undefined;
}

async function startServer() {
  serverPort ||= await reservePort();
  const expectedBaseUrl = `http://127.0.0.1:${serverPort}`;
  const environment = {
    ...process.env,
    PIXELS_CONSOLE_LOCAL_DEVELOPMENT: "1",
    PIXELS_CONSOLE_DATABASE_URL: runtimeDatabaseUrl,
    PIXELS_CONSOLE_LISTEN: `127.0.0.1:${serverPort}`,
    PIXELS_CONSOLE_STATIC_DIRECTORY: path.join(repository, "web/px_console/dist"),
    PIXELS_CONSOLE_PUBLIC_ORIGIN: expectedBaseUrl,
    PIXELS_CONSOLE_REGISTRATION: "1",
    PIXELS_CONSOLE_GUESTS: "1",
    PIXELS_CONSOLE_SESSION_LIFETIME_SECONDS: "3600",
    PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS: "3600",
    PIXELS_CONSOLE_GUEST_SOURCE_KEY: guestSourcePath,
    PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY: workspaceKeyId,
    PIXELS_CONSOLE_WORKSPACE_KEYS: JSON.stringify([{ id: workspaceKeyId, path: workspaceKeyPath }]),
    PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY: recordingCachePath,
    PIXELS_CONSOLE_RECORDING_CACHE_BYTES: "1048576",
    PIXELS_CONSOLE_RECORDING_CACHE_DOWNLOADS: "2",
    PIXELS_CONSOLE_RECORDING_CACHE_TTL_SECONDS: "60",
    PIXELS_CONSOLE_DISTRIBUTION: "customer",
    PIXELS_CONSOLE_MACHINE_SHA256: "a".repeat(64),
    PIXELS_CONSOLE_LICENSE_AUTHORITY_DEPLOYMENT_ID: licenseAuthorityDeploymentId,
    PIXELS_CONSOLE_LICENSE_TRUST_STORE: licenseTrustPath,
    PIXELS_CONSOLE_LICENSE_FILE: licensePath,
    PIXELS_CONSOLE_LICENSE_STATE_DIRECTORY: licenseStatePath,
    PIXELS_CONSOLE_DEPLOYMENT_CERTIFICATE: deploymentCertificatePath,
    PIXELS_CONSOLE_DEPLOYMENT_SIGNING_KEY: deploymentSigningKeyPath,
    PIXELS_CONSOLE_DEPLOYMENT_TRUST_STORE: deploymentTrustPath,
    PIXELS_CONSOLE_DEPLOYMENT_CERTIFICATE_VERSION: "1",
    PIXELS_CONSOLE_DESCRIPTOR_REVISION: "1",
    PIXELS_CONSOLE_DEPLOYMENT_TRUST_EPOCH: "1",
    PIXELS_CONSOLE_MINIMUM_CLIENT_BUILD: "1",
  };
  delete environment.PIXELS_CONSOLE_TLS_CERT;
  delete environment.PIXELS_CONSOLE_TLS_KEY;
  let startupOutput = "";
  child = spawn(consoleExecutable, [], { env: environment, windowsHide: true, stdio: ["ignore", "pipe", "pipe"] });
  baseUrl = await timeLimit(
    new Promise((resolve, reject) => {
      child.once("error", reject);
      child.once("exit", () =>
        reject(new Error(`Console exited before readiness: ${serverOutput.slice(-4000)}`)),
      );
      child.stderr.on("data", (bytes) => {
        serverOutput += bytes.toString();
      });
      child.stdout.on("data", (bytes) => {
        const text = bytes.toString();
        serverOutput += text;
        startupOutput += text;
        const match = startupOutput.match(/Console listening (http:\/\/127\.0\.0\.1:\d+)/);
        if (match) resolve(match[1]);
      });
    }),
    20000,
    "Console startup timeout",
  );
  assert.equal(baseUrl, expectedBaseUrl);
}

async function api(route, method = "GET", body, token, clientType = "admin_web", subjectKind) {
  const response = await fetch(baseUrl + route, {
    method,
    headers: {
      "content-type": "application/json",
      "x-pixels-client-type": clientType,
      origin: baseUrl,
      ...(token ? { authorization: `Bearer ${token}` } : {}),
      ...(subjectKind ? { "x-pixels-subject-kind": subjectKind } : {}),
    },
    body: body === undefined ? undefined : JSON.stringify(body),
    signal: AbortSignal.timeout(22000),
  });
  const text = await response.text();
  return { status: response.status, body: text ? JSON.parse(text) : null };
}

async function connectNode(nodeToken) {
  const socket = new WebSocket(`${baseUrl.replace("http://", "ws://")}/api/console/node-control`);
  await timeLimit(
    new Promise((resolve, reject) => {
      socket.addEventListener("open", resolve, { once: true });
      socket.addEventListener("error", () => reject(new Error("Node WebSocket connection failed")), { once: true });
    }),
    5000,
    "Node WebSocket connection timeout",
  );
  const authenticated = await nodeExchange(socket, {
    type: "authenticate",
    request_id: 1,
    node_token: nodeToken,
  });
  assert.equal(authenticated.type, "authenticated");
  return socket;
}

async function nodeExchange(socket, request) {
  const responsePromise = new Promise((resolve, reject) => {
    const onMessage = (event) => {
      cleanup();
      try {
        resolve(JSON.parse(String(event.data)));
      }
      catch (error) {
        reject(error);
      }
    };
    const onClose = () => {
      cleanup();
      reject(new Error("Node WebSocket closed before responding"));
    };
    const onError = () => {
      cleanup();
      reject(new Error("Node WebSocket exchange failed"));
    };
    const cleanup = () => {
      socket.removeEventListener("message", onMessage);
      socket.removeEventListener("close", onClose);
      socket.removeEventListener("error", onError);
    };
    socket.addEventListener("message", onMessage);
    socket.addEventListener("close", onClose);
    socket.addEventListener("error", onError);
  });
  socket.send(JSON.stringify(request));
  const response = await timeLimit(responsePromise, 5000, `Node request timed out: ${request.type}`);
  assert.equal(response.request_id, request.request_id);
  assert.notEqual(response.type, "error", JSON.stringify(response));
  return response;
}

async function seedRetriedFileTransfer(node, application, deployment, username, administratorToken) {
  const fileName = `browser-transfer-${randomUUID()}.bin`;
  const totalBytes = 12 * 1024 * 1024;
  const cancelledBytes = 4 * 1024 * 1024;
  const expectedSha256 = [...createHash("sha256").update("browser-transfer-payload").digest()];
  const recordingBytes = Buffer.from("browser recording cache payload");
  const recordingFileName = `browser-recording-${randomUUID()}.mp4`;
  const recordingSourceId = randomUUID();
  const recordingSha256 = [...createHash("sha256").update(recordingBytes).digest()];
  const socket = await connectNode(node.body.node_token);
  let requestId = 1;
  const exchange = (request) => nodeExchange(socket, { ...request, request_id: ++requestId });
  try {
    const nodeReport = await exchange({
      type: "report",
      report: {
        sequence: 1,
        product_version_code: 1,
        public_host: "browser-node.example.test",
        desktop_port: 4601,
        application_port_start: 4613,
        application_port_end: 4998,
        game_hook: true,
        webview: true,
        rdp: true,
        rdp_domain: "BROWSER-NODE",
        rdp_proxy_certificate_sha256: "b".repeat(64),
        telemetry: {
          sampled_at: new Date().toISOString(),
          probe_state: "ready",
          logical_processors: 16,
          cpu_utilization_per_mille: 200,
          memory_total_bytes: 64 * 1024 * 1024 * 1024,
          memory_available_bytes: 48 * 1024 * 1024 * 1024,
          disk_total_bytes: 2 * 1024 * 1024 * 1024 * 1024,
          disk_free_bytes: 1024 * 1024 * 1024 * 1024,
          gpu_inventory_revision: 1,
          gpus: [{
            stable_key: "pnp-sha256:browser-validation-gpu",
            name: "Browser validation GPU",
            runtime_binding_ready: true,
            dedicated_memory_bytes: 24 * 1024 * 1024 * 1024,
            used_memory_bytes: 2 * 1024 * 1024 * 1024,
            utilization_per_mille: 100,
            encoder_utilization_per_mille: 50,
          }],
        },
      },
    });
    assert.equal(nodeReport.type, "reported");
    const deploymentReport = await exchange({
      type: "report_deployment",
      deployment_id: deployment.body.id,
      observation: {
        deployment_revision: deployment.body.revision,
        application_revision: deployment.body.application_revision,
        endpoint_revision: nodeReport.endpoint_revision,
        sequence: 1,
        status: { state: "ready" },
      },
    });
    assert.equal(deploymentReport.type, "deployment_reported");
    const reconciliation = await exchange({ type: "begin_reconciliation" });
    assert.equal(reconciliation.type, "reconciliation_started");
    const reconciled = await exchange({
      type: "reconcile",
      inventory: { challenge_id: reconciliation.challenge.id, runtimes: [] },
    });
    assert.equal(reconciled.type, "reconciled");

    const userLogin = await api(
      "/api/console/sessions",
      "POST",
      { username, password: createdUserPassword },
      undefined,
      "user_web",
    );
    assert.equal(userLogin.status, 200);
    assert.match(userLogin.body.token, /^[a-f0-9]{64}$/);
    const userToken = userLogin.body.token;
    const instance = await api(
      "/api/console/instances",
      "POST",
      { request_id: randomUUID(), application_id: application.body.id, deployment_id: deployment.body.id },
      userToken,
      "user_web",
      "user",
    );
    assert.equal(instance.status, 201, JSON.stringify(instance.body));
    const startCommand = await exchange({ type: "poll_command" });
    assert.equal(startCommand.type, "command");
    assert.equal(startCommand.command.action.kind, "start");
    const commandAcknowledgement = await exchange({
      type: "acknowledge_command",
      receipt: {
        command_id: startCommand.command.id,
        lease_id: startCommand.command.lease_id,
        instance_id: startCommand.command.instance_id,
        launch_id: startCommand.command.launch_id,
        instance_revision: startCommand.command.instance_revision,
        outcome: { result: "running", port: startCommand.command.action.port },
      },
    });
    assert.equal(commandAcknowledgement.state, "running");
    const resourceSession = await api(
      "/api/console/resource-sessions",
      "POST",
      {
        request_id: randomUUID(),
        target: {
          kind: "cloud_application",
          application_id: application.body.id,
          instance_id: instance.body.id,
        },
        access: "controller",
      },
      userToken,
      "user_web",
      "user",
    );
    assert.equal(resourceSession.status, 201, JSON.stringify(resourceSession.body));
    const descriptor = await api(
      `/api/console/resource-sessions/${resourceSession.body.id}/descriptor`,
      "POST",
      { revision: resourceSession.body.revision },
      userToken,
      "user_web",
      "user",
    );
    assert.equal(descriptor.status, 200, JSON.stringify(descriptor.body));
    const admitted = await exchange({
      type: "admit_frontend",
      session_id: resourceSession.body.id,
      revision: descriptor.body.descriptor.session.revision,
      frontend_token: descriptor.body.token,
    });
    assert.equal(admitted.type, "frontend_admitted");
    const cancelledTransfer = await exchange({
      type: "begin_file_transfer",
      transfer: {
        transfer_request_id: randomUUID(),
        session_id: resourceSession.body.id,
        direction: "from_node",
        file_name: fileName,
        total_bytes: totalBytes,
        expected_sha256: expectedSha256,
      },
    });
    assert.equal(cancelledTransfer.type, "file_transfer_started");
    const cancelled = await exchange({
      type: "report_file_transfer",
      transfer_id: cancelledTransfer.transfer_id,
      progress: {
        sequence: 1,
        transferred_bytes: cancelledBytes,
        outcome: { kind: "cancelled" },
      },
    });
    assert.equal(cancelled.state, "cancelled");
    const retriedTransfer = await exchange({
      type: "begin_file_transfer",
      transfer: {
        transfer_request_id: randomUUID(),
        session_id: resourceSession.body.id,
        direction: "from_node",
        file_name: fileName,
        total_bytes: totalBytes,
        expected_sha256: expectedSha256,
      },
    });
    assert.equal(retriedTransfer.type, "file_transfer_started");
    assert.notEqual(retriedTransfer.transfer_id, cancelledTransfer.transfer_id);
    const completed = await exchange({
      type: "report_file_transfer",
      transfer_id: retriedTransfer.transfer_id,
      progress: {
        sequence: 1,
        transferred_bytes: totalBytes,
        outcome: { kind: "completed", received_sha256: expectedSha256 },
      },
    });
    assert.equal(completed.state, "completed");
    const transfers = await api(
      "/api/console/file-transfers?limit=100",
      "GET",
      undefined,
      userToken,
      "user_web",
      "user",
    );
    assert.equal(transfers.status, 200);
    const persistedCancelled = transfers.body.find((candidate) => candidate.id === cancelledTransfer.transfer_id);
    assert.equal(persistedCancelled.file_name, fileName);
    assert.equal(persistedCancelled.transferred_bytes, cancelledBytes);
    assert.equal(persistedCancelled.state, "cancelled");
    const persistedRetry = transfers.body.find((candidate) => candidate.id === retriedTransfer.transfer_id);
    assert.equal(persistedRetry.file_name, fileName);
    assert.equal(persistedRetry.transferred_bytes, totalBytes);
    assert.equal(persistedRetry.state, "completed");

    const recording = await exchange({
      type: "report_recording",
      recording: {
        source_id: recordingSourceId,
        source_sha256: recordingSha256,
        session_id: resourceSession.body.id,
        file_name: recordingFileName,
        size_bytes: recordingBytes.length,
        modified_unix_ms: Date.now(),
        codec: "h264",
        sequence: 1,
        present: true,
      },
    });
    assert.equal(recording.type, "recording_reported");
    const cacheRequest = await api(
      `/api/console/managed/recordings/${recording.recording_id}/cache`,
      "POST",
      null,
      administratorToken,
    );
    assert.equal(cacheRequest.status, 200, JSON.stringify(cacheRequest.body));
    assert.equal(cacheRequest.body.state, "fetching");
    const cacheUploads = await exchange({ type: "poll_recording_cache", after: null, limit: 4 });
    assert.equal(cacheUploads.type, "recording_cache_uploads");
    assert.equal(cacheUploads.uploads.length, 1);
    const cacheUpload = cacheUploads.uploads[0];
    assert.equal(cacheUpload.recording_id, recording.recording_id);
    const uploaded = await fetch(baseUrl + cacheUpload.upload_path, {
      method: "PUT",
      headers: {
        authorization: `Bearer ${cacheUpload.upload_token}`,
        "content-type": "application/octet-stream",
      },
      body: recordingBytes,
      signal: AbortSignal.timeout(22000),
    });
    assert.equal(uploaded.status, 201);
    const publishedCache = await uploaded.json();
    assert.equal(publishedCache.state, "ready");
    assert.equal(publishedCache.pinned, false);
    assert.equal(publishedCache.size_bytes, recordingBytes.length);
    return {
      cancelledBytes,
      fileName,
      recordingFileName,
      recordingId: recording.recording_id,
      totalBytes,
    };
  }
  finally {
    socket.close(1000, "browser fixture complete");
  }
}

async function run() {
  createBrowserDatabase();
  provisionConsole();
  await startServer();
  assert.equal((await api("/health/ready")).status, 204);

  browser = await chromium.launch({ headless: true });
  const context = await browser.newContext();
  await context.addInitScript(() => localStorage.setItem("language", "en"));
  const externalRequests = [];
  await context.route("**/*", (route) => {
    if (new URL(route.request().url()).origin === baseUrl) return route.continue();
    externalRequests.push(route.request().url());
    return route.abort();
  });
  const page = await context.newPage();
  page.setDefaultTimeout(15000);
  const pageErrors = [];
  page.on("pageerror", (error) => pageErrors.push(error.message));
  const selectAdministratorMenu = (label) =>
    page.locator(".ant-menu-item").filter({ hasText: label }).click();

  const documentResponse = await page.goto(baseUrl);
  assert.equal(documentResponse.status(), 200);
  const usernameInput = page.locator('input[autocomplete="username"]');
  const passwordInput = page.locator('input[autocomplete="current-password"]');
  const signInButton = page.getByRole("button", { name: "Sign in", exact: true });
  await usernameInput.fill(initialUsername);
  await passwordInput.fill(initialPassword);
  assert.equal(await usernameInput.inputValue(), initialUsername);
  assert.equal(await passwordInput.inputValue(), initialPassword);
  assert.equal(await signInButton.isDisabled(), false);
  const loginRequested = page.waitForResponse(
    (response) => response.url().endsWith("/api/console/sessions") && response.request().method() === "POST",
  );
  await signInButton.click();
  const loginResponse = await loginRequested;
  assert.equal(loginResponse.status(), 200, await loginResponse.text());
  await page.waitForTimeout(2000);
  if (new URL(page.url()).pathname !== "/resources") {
    const retainedToken = await page.evaluate(() => sessionStorage.getItem("pixels.admin_web.token"));
    const directSession = retainedToken ? await api("/api/console/session", "GET", undefined, retainedToken) : { status: 0, body: null };
    throw new Error(
      `Console login did not navigate: url=${page.url()} retained_token=${Boolean(retainedToken)} session_status=${directSession.status} errors=${JSON.stringify(pageErrors)} body=${(await page.locator("body").innerText()).slice(0, 1000)}`,
    );
  }
  await page.getByText("Resource overview", { exact: true }).first().waitFor();
  await page.getByText("Live", { exact: true }).waitFor();
  assert.equal(new URL(page.url()).pathname, "/resources");
  const administratorToken = await page.evaluate(() => sessionStorage.getItem("pixels.admin_web.token"));
  assert.match(administratorToken, /^[a-f0-9]{64}$/);
  console.log("PASS console-browser/login-dashboard");

  const createdUsername = `browser-user-${randomUUID()}`;
  await selectAdministratorMenu("Users");
  await page.getByRole("button", { name: "Create user", exact: true }).click();
  const userDialog = page.getByRole("dialog");
  const createdUsernameInput = userDialog.locator('input[maxlength="64"]');
  const createdPasswordInput = userDialog.locator('input[maxlength="128"][type="password"]');
  await createdUsernameInput.fill(createdUsername);
  await createdPasswordInput.fill(createdUserPassword);
  assert.equal(await createdUsernameInput.inputValue(), createdUsername);
  assert.equal(await createdPasswordInput.inputValue(), createdUserPassword);
  const userCreated = page.waitForResponse(
    (response) => response.url().endsWith("/api/console/users") && response.request().method() === "POST",
  );
  await userDialog.getByRole("button", { name: "Save", exact: true }).click();
  assert.equal((await userCreated).status(), 201);
  await page.getByRole("cell", { name: createdUsername, exact: true }).waitFor();

  const groupName = `Browser group ${randomUUID()}`;
  await selectAdministratorMenu("Groups");
  await page.getByRole("button", { name: "Create group", exact: true }).click();
  const groupDialog = page.getByRole("dialog");
  await groupDialog.locator('input[maxlength="128"]').fill(groupName);
  await groupDialog.locator("textarea").fill("Synthetic browser group");
  const groupCreated = page.waitForResponse(
    (response) => response.url().endsWith("/api/console/groups") && response.request().method() === "POST",
  );
  await groupDialog.getByRole("button", { name: "Save", exact: true }).click();
  assert.equal((await groupCreated).status(), 201);
  await page.getByRole("cell", { name: groupName, exact: true }).waitFor();
  console.log("PASS console-browser/identity-create-user-group");

  const deviceName = `Browser device ${randomUUID()}`;
  await selectAdministratorMenu("Devices");
  await page.getByRole("button", { name: "Register device", exact: true }).click();
  const deviceDialog = page.getByRole("dialog");
  await deviceDialog.locator('input[maxlength="128"]').fill(deviceName);
  const deviceCreated = page.waitForResponse(
    (response) => response.url().endsWith("/api/console/managed/devices") && response.request().method() === "POST",
  );
  await deviceDialog.getByRole("button", { name: "Save", exact: true }).click();
  const deviceResponse = await deviceCreated;
  assert.equal(deviceResponse.status(), 201);
  const device = await deviceResponse.json();
  assert.match(device.enrollment_token, /^[a-f0-9]{64}$/);
  const credentialDialog = page.getByRole("dialog", { name: "One-time device enrollment token" });
  await credentialDialog.getByText(device.enrollment_token, { exact: true }).waitFor();
  await credentialDialog.getByRole("button", { name: "Close", exact: true }).click();
  await credentialDialog.waitFor({ state: "hidden" });
  await page.getByRole("cell", { name: deviceName, exact: true }).waitFor();

  const realtimeDeviceName = `Realtime device ${randomUUID()}`;
  const realtimeDevice = await api(
    "/api/console/managed/devices",
    "POST",
    { name: realtimeDeviceName, platform: "windows" },
    administratorToken,
  );
  assert.equal(realtimeDevice.status, 201);
  await page.getByRole("cell", { name: realtimeDeviceName, exact: true }).waitFor();
  console.log("PASS console-browser/device-one-time-enrollment");

  await page.getByTitle("Switch to dark theme").click();
  assert.equal(await page.locator("html").evaluate((element) => element.classList.contains("dark")), true);
  await page.getByTitle("Language").click();
  await page.getByRole("menuitem", { name: "简体中文", exact: true }).click();
  await page.locator(".ant-menu-item").filter({ hasText: "设备列表" }).waitFor();
  await page.getByTitle("语言").click();
  await page.getByRole("menuitem", { name: "English", exact: true }).click();
  await page.locator(".ant-menu-item").filter({ hasText: "Devices" }).waitFor();
  assert.equal(await page.evaluate(() => sessionStorage.getItem("pixels.admin_web.token")), administratorToken);
  console.log("PASS console-browser/navigation-language-theme");

  const previewNode = await api(
    "/api/console/managed/nodes",
    "POST",
    { device_id: device.device.id, product: "cloud_node", max_instances: 4 },
    administratorToken,
  );
  assert.equal(previewNode.status, 201);
  const previewApplicationName = `Preview application ${randomUUID()}`;
  const previewApplication = await api(
    "/api/console/managed/applications",
    "POST",
    {
      name: previewApplicationName,
      access: "public",
      launch: {
        kind: "webview",
        entry_url: "https://example.test/preview",
        video: { codec: "h264", bitrate_kbps: 8000 },
      },
      allow_observer: false,
      allow_takeover: false,
      disabled: false,
    },
    administratorToken,
  );
  assert.equal(previewApplication.status, 201);
  const previewDeployment = await api(
    "/api/console/managed/deployments",
    "POST",
    {
      application_id: previewApplication.body.id,
      node_id: previewNode.body.node.id,
      configuration: {
        target: { kind: "webview" },
        gpu_key: null,
        gpu_profile: {
          memory_bytes: 536870912,
          compute_per_mille: 100,
          encoder_per_mille: 100,
          memory_reserve_bytes: 536870912,
          compute_limit_per_mille: 900,
          encoder_limit_per_mille: 900,
        },
        capacity: 2,
        disabled: false,
      },
    },
    administratorToken,
  );
  assert.equal(previewDeployment.status, 201);
  await page.goto(baseUrl + "/apps");
  const schedulingCard = page.locator(".ant-card").filter({ hasText: "Scheduling preview and rejection reasons" });
  await schedulingCard.getByText("Scheduling preview and rejection reasons", { exact: true }).waitFor();
  await schedulingCard.getByText(previewApplicationName, { exact: true }).waitFor();
  const placementRequested = page.waitForResponse(
    (response) =>
      response.url().endsWith("/api/console/managed/scheduling/preview") && response.request().method() === "POST",
  );
  await schedulingCard.getByRole("button", { name: "Preview placement", exact: true }).click();
  assert.equal((await placementRequested).status(), 200);
  await schedulingCard.getByText("Node not ready", { exact: true }).waitFor();
  await schedulingCard.getByText("Node disconnected", { exact: true }).waitFor();
  console.log("PASS console-browser/scheduling-preview-rejections");

  const nodeCard = page.locator(".ant-card").filter({ hasText: "Node identities and status" });
  const previewNodeRow = nodeCard.getByRole("row").filter({ hasText: deviceName });
  const rawTelemetryRequested = page.waitForResponse(
    (response) =>
      new URL(response.url()).pathname === `/api/console/managed/nodes/${previewNode.body.node.id}/telemetry` &&
      response.request().method() === "GET",
  );
  const trendRequested = page.waitForResponse(
    (response) =>
      new URL(response.url()).pathname ===
        `/api/console/managed/nodes/${previewNode.body.node.id}/telemetry/trend` &&
      response.request().method() === "GET",
  );
  await previewNodeRow.getByRole("button", { name: "History", exact: true }).click();
  assert.equal((await rawTelemetryRequested).status(), 200);
  assert.equal((await trendRequested).status(), 200);
  const telemetryDialog = page.getByRole("dialog", { name: `Telemetry history — ${deviceName}` });
  await telemetryDialog.getByText("Latest accepted sample is stale (Unknown old)", { exact: true }).waitFor();
  await telemetryDialog.getByText("0/0 known", { exact: true }).first().waitFor();
  await telemetryDialog.locator("button.ant-modal-close").click();
  console.log("PASS console-browser/server-telemetry-trend");

  const transferFixture = await seedRetriedFileTransfer(
    previewNode,
    previewApplication,
    previewDeployment,
    createdUsername,
    administratorToken,
  );
  console.log("PASS console-protocol/completed-file-transfer-fixture");
  console.log("PASS console-protocol/cancelled-file-transfer-remains-terminal-after-retry");
  console.log("PASS console-protocol/recording-cache-ready-fixture");

  const cacheBeforeUi = await api("/api/console/managed/recording-cache?limit=100", "GET", undefined, administratorToken);
  assert.equal(cacheBeforeUi.status, 200);
  const readyCache = cacheBeforeUi.body.find((candidate) => candidate.recording_id === transferFixture.recordingId);
  assert.equal(readyCache?.state, "ready", JSON.stringify(cacheBeforeUi.body));
  const managedRecordingsRequested = page.waitForResponse(
    (response) => new URL(response.url()).pathname === "/api/console/managed/recordings" && response.request().method() === "GET",
  );
  const managedRecordingCacheRequested = page.waitForResponse(
    (response) => new URL(response.url()).pathname === "/api/console/managed/recording-cache" && response.request().method() === "GET",
  );
  await page.goto(baseUrl + "/security-internal");
  assert.equal((await managedRecordingsRequested).status(), 200);
  assert.equal((await managedRecordingCacheRequested).status(), 200);
  await page.getByRole("tab", { name: "Recording metadata", exact: true }).click();
  const recordingRow = page.getByRole("row").filter({ hasText: transferFixture.recordingFileName });
  await recordingRow.getByText(transferFixture.recordingFileName, { exact: true }).waitFor();
  await recordingRow.getByText("ready", { exact: true }).waitFor();
  await recordingRow.getByRole("button", { name: "Retain", exact: true }).click();
  await page.getByText("The cache copy will be retained.", { exact: true }).waitFor();
  await recordingRow.getByRole("button", { name: "Release", exact: true }).waitFor();
  assert.equal(await recordingRow.getByRole("button", { name: "Evict copy", exact: true }).isDisabled(), true);
  await recordingRow.getByRole("button", { name: "Release", exact: true }).click();
  await page.getByText("The cache copy is no longer retained.", { exact: true }).waitFor();
  await recordingRow.getByRole("button", { name: "Evict copy", exact: true }).click();
  await page.getByRole("button", { name: "OK", exact: true }).click();
  await page.getByText("The Console cache copy was removed.", { exact: true }).waitFor();
  await recordingRow.getByText("Not cached", { exact: true }).waitFor();
  console.log("PASS console-browser/managed-recording-retain-release-evict");

  await stopServer();
  await page.getByText("Reconnecting", { exact: true }).waitFor();
  await startServer();
  assert.equal((await api("/api/console/session", "GET", undefined, administratorToken)).status, 200);
  await page.getByText("Live", { exact: true }).waitFor();
  console.log("PASS console-browser/management-realtime-refresh-reconnect");
  const usersAfterRestart = await api("/api/console/users?limit=100", "GET", undefined, administratorToken);
  assert.equal(usersAfterRestart.status, 200);
  assert.ok(usersAfterRestart.body.some((user) => user.username === createdUsername));
  await page.goto(baseUrl + "/resources");
  await page.getByText("Resource overview", { exact: true }).first().waitFor();
  console.log("PASS console-process/restart-preserves-session-data");

  const databaseAuthorityExit = once(child, "exit");
  docker("stop", "--time", "10", container);
  containerStopped = true;
  await timeLimit(databaseAuthorityExit, 20000, "Console did not terminate after database authority loss");
  child = undefined;
  await assert.rejects(
    api("/api/console/session", "GET", undefined, administratorToken),
    /fetch failed|ECONNREFUSED/,
  );
  docker("start", container);
  containerStopped = false;
  const recoveryDeadline = Date.now() + 30000;
  while (docker("inspect", container, "--format", "{{.State.Health.Status}}").trim() !== "healthy") {
    if (Date.now() >= recoveryDeadline) throw new Error("Console database recovery deadline");
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
  await startServer();
  assert.equal((await api("/health/ready")).status, 204);
  assert.equal((await api("/api/console/session", "GET", undefined, administratorToken)).status, 200);
  console.log("PASS console-process/database-outage-fails-closed-and-recovers");

  await page.goto(baseUrl + "/profile-info");
  await page.getByRole("button", { name: "Sign out", exact: true }).click();
  await page.waitForURL(baseUrl + "/");
  assert.equal((await api("/api/console/session", "GET", undefined, administratorToken)).status, 403);
  await page.goto(baseUrl + "/user/login");
  await page.locator('input[autocomplete="username"]').fill(createdUsername);
  await page.locator('input[autocomplete="current-password"]').fill(createdUserPassword);
  await page.locator('button[type="submit"]').click();
  await page.waitForURL(baseUrl + "/user/home");
  const userToken = await page.evaluate(() => sessionStorage.getItem("pixels.user_web.token"));
  assert.match(userToken, /^[a-f0-9]{64}$/);
  const ownedRecordingsRequested = page.waitForResponse(
    (response) => new URL(response.url()).pathname === "/api/console/recordings" && response.request().method() === "GET",
  );
  await page.locator(".ant-menu-item").filter({ hasText: "My recordings" }).click();
  assert.equal((await ownedRecordingsRequested).status(), 200);
  await page.getByText("My recordings", { exact: true }).first().waitFor();
  assert.equal(new URL(page.url()).pathname, "/user/recordings");
  console.log("PASS console-browser/user-recordings-empty-state");
  const ownedTransfersRequested = page.waitForResponse(
    (response) => new URL(response.url()).pathname === "/api/console/file-transfers" && response.request().method() === "GET",
  );
  await page.locator(".ant-menu-item").filter({ hasText: "Instances and activity" }).click();
  assert.equal((await ownedTransfersRequested).status(), 200);
  await page.getByRole("tab", { name: "File transfers", exact: true }).click();
  await page
    .getByText(
      "Only file transfers attributed to your resource sessions are shown. This is audit history, not a resumable task queue.",
      { exact: true },
    )
    .waitFor();
  const transferRows = page.getByRole("row").filter({ hasText: transferFixture.fileName });
  const completedTransferRow = transferRows.filter({ hasText: "Completed" });
  await completedTransferRow.getByText(transferFixture.fileName, { exact: true }).waitFor();
  await completedTransferRow.getByText("Download from node", { exact: true }).waitFor();
  await completedTransferRow.getByText("Completed", { exact: true }).waitFor();
  await completedTransferRow.getByText("12.00 MB / 12.00 MB", { exact: true }).waitFor();
  const cancelledTransferRow = transferRows.filter({ hasText: "Cancelled" });
  await cancelledTransferRow.getByText(transferFixture.fileName, { exact: true }).waitFor();
  await cancelledTransferRow.getByText("Download from node", { exact: true }).waitFor();
  await cancelledTransferRow.getByText("Cancelled", { exact: true }).waitFor();
  await cancelledTransferRow.getByText("4.00 MB / 12.00 MB", { exact: true }).waitFor();
  assert.equal(new URL(page.url()).pathname, "/user/activity");
  console.log("PASS console-browser/user-file-transfers-completed-state");
  console.log("PASS console-browser/user-file-transfers-cancelled-retry-state");
  assert.deepEqual(pageErrors, []);
  assert.deepEqual(externalRequests, []);
  assert.ok(!serverOutput.includes(initialPassword) && !serverOutput.includes(createdUserPassword));
  console.log("PASS console-browser/logout-revokes");
}

run()
  .catch((error) => {
    console.error(error.stack || error.message);
    process.exitCode = 1;
  })
  .finally(async () => {
    if (browser) await browser.close();
    await stopServer();
    if (containerStopped) docker("start", container);
    if (databaseCreated) {
      docker(
        "exec",
        container,
        "psql",
        "-X",
        "-v",
        "ON_ERROR_STOP=1",
        "-U",
        "pixels_admin",
        "-d",
        "postgres",
        "-c",
        `DROP DATABASE IF EXISTS ${databaseName} WITH (FORCE)`,
      );
    }
    const resolvedPrivateDirectory = path.resolve(privateDirectory);
    assert.equal(path.dirname(resolvedPrivateDirectory), path.resolve(os.tmpdir()));
    assert.match(path.basename(resolvedPrivateDirectory), /^pixels-console-browser-/);
    fs.rmSync(resolvedPrivateDirectory, { recursive: true, force: true });
  });
