// Disposable Console process + actual built Vue application + isolated PostgreSQL database.
const assert = require("node:assert/strict");
const { execFileSync, spawn } = require("node:child_process");
const { randomUUID } = require("node:crypto");
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
const workspaceKeyId = randomUUID();
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

async function api(route, method = "GET", body, token) {
  const response = await fetch(baseUrl + route, {
    method,
    headers: {
      "content-type": "application/json",
      "x-pixels-client-type": "admin_web",
      origin: baseUrl,
      ...(token ? { authorization: `Bearer ${token}` } : {}),
    },
    body: body === undefined ? undefined : JSON.stringify(body),
    signal: AbortSignal.timeout(22000),
  });
  const text = await response.text();
  return { status: response.status, body: text ? JSON.parse(text) : null };
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
    for (const file of [guestSourcePath, workspaceKeyPath, passwordPath]) {
      if (fs.existsSync(file)) fs.unlinkSync(file);
    }
    if (fs.existsSync(recordingCachePath)) fs.rmSync(recordingCachePath, { recursive: true });
    fs.rmdirSync(privateDirectory);
  });
