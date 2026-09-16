// Run from repository root: node docs/tests/server_topology.test.cjs
// Uses the existing Console development dependency; never connects to a backend.
const { chromium } = require("../../web/px_console/node_modules/playwright");
const assert = require("node:assert/strict");
const path = require("node:path");
const fs = require("node:fs");
const { pathToFileURL } = require("node:url");

(async () => {
  fs.mkdirSync(path.resolve(__dirname, "../../.cache"), { recursive: true });
  const browser = await chromium.launch({
    executablePath: process.env.PIXELS_TEST_CHROME || "C:/Program Files/Google/Chrome/Application/chrome.exe",
    headless: true,
  });
  try {
    const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } });
    const errors = [];
    const requests = [];
    page.on("pageerror", (error) => errors.push(error.message));
    page.on("request", (request) => {
      if (/^https?:/.test(request.url())) requests.push(request.url());
    });
    const url = `${pathToFileURL(path.resolve(__dirname, "../../docs/server_topology.html")).href}#business`;
    await page.goto(url);
    assert.equal(await page.locator("#business").isVisible(), true);
    const shape = await page.evaluate(() => {
      const keys = (value, prefix = "") =>
        Object.entries(value)
          .flatMap(([key, item]) => {
            const name = `${prefix}.${key}`;
            return item && typeof item === "object" ? keys(item, name) : [name];
          })
          .sort();
      return [keys(catalogs.zh), keys(catalogs.en)];
    });
    assert.deepEqual(shape[0], shape[1], "Nested localization catalog parity");
    assert.equal(await page.locator('footer a[href="postgresql_database_migration_plan.md"]').count(), 1);
    for (const language of ["zh", "en"]) {
      if (language === "en") await page.locator("#language").click();
      await page.locator('[data-business-app="game"]').click();
      await page.locator("#businessScenario").selectOption("normal");
      await page.locator("#businessReset").click();
      assert.match(await page.locator("#businessRecommendation").innerText(), /machine-a \/ GPU 1/);
      assert.equal(await page.locator('[data-candidate="machine-a/a-g0"]').getAttribute("data-eligible"), "false");
      assert.equal(await page.locator('[data-candidate="machine-d/d-g0"]').getAttribute("data-eligible"), "false");
      await page.locator("#businessRole").selectOption("viewer");
      assert.equal(await page.locator("#businessReserve").isDisabled(), true);
      await page.locator("#businessRole").selectOption("operator");
      await page.locator("#businessReserve").click();
      assert.equal(await page.locator("#businessReservationCount").innerText(), "1");
      assert.match(await page.locator("#businessRecommendation").innerText(), /machine-b \/ GPU 0/);
      await page.locator("#businessReserve").click();
      assert.equal(await page.locator("#businessReservationCount").innerText(), "2");
      assert.equal(await page.locator("#businessReserve").isDisabled(), true);
      for (const scenario of ["reserved", "cpu", "drain"]) {
        await page.locator("#businessScenario").selectOption(scenario);
        assert.match(await page.locator("#businessRecommendation").innerText(), /machine-b \/ GPU 0/);
      }
      for (const scenario of ["stale", "full"]) {
        await page.locator("#businessScenario").selectOption(scenario);
        assert.equal(await page.locator("#businessReserve").isDisabled(), true);
        assert.equal(await page.locator('[data-eligible="true"]').count(), 0);
      }
      await page.locator("#businessScenario").selectOption("normal");
      await page.locator("#businessMachine").selectOption("machine-c");
      assert.equal(await page.locator("#businessReserve").isDisabled(), true, "Missing deployment cannot be forced");
      await page.locator("#businessMachine").selectOption("auto");
      for (const app of ["app", "web"]) {
        await page.locator('[data-business-app="' + app + '"]').click();
        await page.locator("#businessReset").click();
        assert.match(await page.locator("#businessRecommendation").innerText(), /machine-c \/ GPU 0/);
        await page.locator("#businessReserve").click();
        assert.match(await page.locator("#businessRecommendation").innerText(), /machine-a \/ GPU 1/);
      }
      await page.locator("#businessReset").click();
      await page.locator('[data-business-app="rdp"]').click();
      assert.match(await page.locator("#businessRecommendation").innerText(), /machine-b \/ workspace-01/);
      assert.equal(await page.locator('[data-eligible="true"]').count(), 1);
      await page.locator("#businessMachine").selectOption("machine-a");
      assert.equal(await page.locator("#businessReserve").isDisabled(), true, "RDP owner cannot be overridden");
      await page.locator("#businessMachine").selectOption("auto");
      await page.locator("#businessReserve").click();
      assert.equal(await page.locator("#businessReserve").isDisabled(), true, "Second frontend reservation rejected");
      await page.locator("#businessReset").click();
      await page.locator("#businessScenario").selectOption("stale");
      assert.equal(await page.locator("#businessReserve").isDisabled(), true);
      await page.locator("#businessScenario").selectOption("normal");
      await page.locator('[data-business-app="game"]').click();
      for (const width of [390, 540, 800, 1050, 1920]) {
        await page.setViewportSize({ width, height: 1000 });
        assert.ok(
          await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth),
          `${language} overflow at ${width}px`,
        );
      }
      await page.setViewportSize({ width: 1440, height: 1100 });
      await page.locator('[data-view="upgrade"]').click();
      for (const scenario of ["redundant", "single", "node"]) {
        await page.locator(`[data-scenario="${scenario}"]`).click();
        await page.locator("#simReset").click();
        if (scenario === "node") await page.locator("#users").fill("2");
        await page.locator("#simNext").click();
        if (scenario === "node") {
          assert.equal(await page.locator("#simNext").isDisabled(), true, "Active users block node activation");
          await page.locator("#users").fill("1");
          assert.equal(await page.locator("#simNext").isDisabled(), true, "Even one user blocks activation");
          await page.locator("#users").fill("0");
          assert.equal(await page.locator("#simNext").isDisabled(), false);
        }
        await page.locator("#simNext").click();
        if (scenario === "node") assert.equal(await page.locator("#users").isDisabled(), true);
        await page.locator("#simNext").click();
        assert.equal(await page.locator("#simNext").isDisabled(), true);
        await page.locator("#simReset").click();
        assert.equal(await page.locator("#simNext").isDisabled(), false);
      }
      await page.locator('[data-view="operations"]').click();
      for (const service of [
        "overview",
        "console",
        "auth",
        "desk",
        "broker",
        "relay",
        "node",
        "render",
        "rdp",
        "maintenance",
      ]) {
        await page.locator(`[data-ops-page="${service}"]`).click();
        assert.ok((await page.locator("#opsTable tbody tr").count()) > 0);
        assert.ok((await page.locator("#opsDetail").innerText()).length > 60);
      }
      await page.locator('[data-ops-page="relay"]').click();
      await page.locator("#opsFilter").check();
      assert.equal(await page.locator("#opsTable tbody tr").count(), 1);
      await page.locator("#opsPreviewButton").click();
      assert.equal(await page.locator("#opsPreflight").isVisible(), true);
      await page.locator('[data-role="viewer"]').click();
      assert.equal(await page.locator("#opsPreviewButton").isDisabled(), true, "Read-only role blocks operations");
      assert.equal(await page.locator("#opsPreflight").isVisible(), false);
      await page.locator('[data-role="operator"]').click();
      await page.locator("#opsPreviewButton").click();
      await page.locator("#opsStale").click();
      assert.equal(await page.locator("#opsPreviewButton").isDisabled(), true, "Stale state blocks operations");
      assert.equal(await page.locator("#opsPreflight").isVisible(), false);
      assert.equal(await page.locator("#opsTable .ops-badge:not(.unknown)").count(), 0);
      assert.equal(await page.locator("#opsMetrics strong").first().innerText(), "—");
      await page.locator("#opsStale").click();
      await page.locator("#opsFilter").uncheck();
      await page.locator('[data-ops-page="node"]').click();
      await page.locator('[data-ops-row="node-01"]').click();
      await page.locator("#opsPreviewButton").click();
      await page.locator('[data-ops-row="node-02"]').click();
      assert.equal(await page.locator("#opsPreviewButton").isDisabled(), true, "Unknown target blocks operations");
      assert.equal(await page.locator("#opsPreflight").isVisible(), false);
      await page.locator('[data-ops-row="node-01"]').click();
      assert.equal(await page.locator("#opsPreviewButton").isDisabled(), false);
      await page.locator('[data-ops-page="console"]').click();
      await page.locator("#opsFilter").check();
      assert.equal(await page.locator("#opsTable tbody tr").count(), 0);
      assert.equal(await page.locator("#opsPreviewButton").isDisabled(), true, "Empty selection blocks operations");
      await page.locator("#opsFilter").uncheck();
      // Deployment selection lives on the topology view, and applies to operations too.
      await page.locator('[data-ops-page="auth"]').click();
      await page.locator('[data-view="overview"]').click();
      await page.locator('[data-deploy="private"]').click();
      await page.locator('[data-view="operations"]').click();
      assert.equal(await page.locator('[data-ops-page="auth"]').count(), 0);
      assert.equal(await page.locator('[data-ops-row="auth-01"]').count(), 0);
      await page.locator('[data-view="overview"]').click();
      await page.locator('[data-deploy="official"]').click();
      for (const width of [390, 540, 800, 1050, 1920]) {
        await page.setViewportSize({ width, height: 1000 });
        for (const view of ["upgrade", "operations"]) {
          await page.locator(`[data-view="${view}"]`).click();
          assert.ok(
            await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth),
            `${language} ${view} overflow at ${width}px`,
          );
        }
      }
      await page.locator('[data-view="business"]').click();
    }
    // Exercise the pure model's per-GPU and cross-GPU host gates with synthetic budgets.
    const gates = await page.evaluate(() => {
      state.businessApp = "app";
      state.businessReservations = [];
      state.businessMachine = "machine-a";
      const gpu = businessHosts[0].gpus[1];
      const oldVram = gpu.vram;
      gpu.vram = 19;
      const vramRejected = businessCandidates()
        .find((row) => row.gpu === gpu.id)
        .reasons.includes("vram");
      gpu.vram = oldVram;
      state.businessReservations = [
        { host: "machine-a", gpu: "a-g0", app: "app", profile: { ...businessProfiles.app, cpu: 60 } },
      ];
      const hostRejected = businessCandidates().every((row) => row.reasons.includes("cpu"));
      state.businessReservations = [];
      state.businessMachine = "auto";
      state.businessApp = "game";
      render();
      return { vramRejected, hostRejected };
    });
    assert.deepEqual(gates, { vramRejected: true, hostRejected: true });
    // Test each hard threshold in isolation: equality admits, one unit over rejects for that reason alone.
    const boundaries = await page.evaluate(() => {
      const savedState = { ...state };
      const host = businessHosts[0];
      const gpu = host.gpus[1];
      const savedHost = { ...host };
      const savedGpu = { ...gpu };
      const results = [];
      try {
        Object.assign(state, {
          businessApp: "app",
          businessScenario: "normal",
          businessMachine: host.id,
          businessReservations: [],
        });
        const profile = businessProfiles.app;
        const cases = [
          [host, "cpu", 80, profile.cpu, "cpu"],
          [host, "ram", 64, profile.ram, "ram"],
          [host, "net", 100, profile.net, "net"],
          [gpu, "gpu", 85, profile.gpu, "gpu"],
          [gpu, "vram", 20, profile.vram, "vram"],
          [gpu, "enc", 2, profile.enc, "enc"],
          [gpu, "slots", 3, 1, "slots"],
        ];
        for (const [target, key, limit, demand, reason] of cases) {
          const before = target[key];
          target[key] = limit - demand;
          const atLimit = [...businessCandidates().find((row) => row.gpu === gpu.id).reasons];
          target[key] += 1;
          const overLimit = [...businessCandidates().find((row) => row.gpu === gpu.id).reasons];
          target[key] = before;
          results.push({ reason, atLimit, overLimit });
        }
      } finally {
        Object.assign(host, savedHost);
        Object.assign(gpu, savedGpu);
        Object.assign(state, savedState);
        render();
      }
      return results;
    });
    for (const { reason, atLimit, overLimit } of boundaries) {
      assert.deepEqual(atLimit, [], `${reason}: exact capacity must admit`);
      assert.deepEqual(overLimit, [reason], `${reason}: independent overflow rejection`);
    }
    await page.locator("#language").click();
    await page.setViewportSize({ width: 1440, height: 1100 });
    await page.locator('[data-view="operations"]').click();
    await page.locator('[data-ops-page="auth"]').click();
    await page.screenshot({ path: path.resolve(__dirname, "../../.cache/server-operations-auth.png"), fullPage: true });
    await page.locator('[data-view="business"]').click();
    await page.screenshot({
      path: path.resolve(__dirname, "../../.cache/server-business-desktop.png"),
      fullPage: true,
    });
    await page.locator("#theme").click();
    await page.screenshot({ path: path.resolve(__dirname, "../../.cache/server-business-dark.png"), fullPage: true });
    await page.setViewportSize({ width: 390, height: 844 });
    await page.screenshot({ path: path.resolve(__dirname, "../../.cache/server-business-mobile.png"), fullPage: true });
    for (const view of ["overview", "connection", "upgrade", "operations", "business"]) {
      await page.locator(`[data-view="${view}"]`).click();
      const section = {
        overview: "architecture",
        connection: "architecture",
        upgrade: "upgrades",
        operations: "operations",
        business: "business",
      }[view];
      assert.equal(await page.locator(`#${section}`).isVisible(), true);
    }
    const ids = await page.locator("[id]").evaluateAll((elements) => elements.map((element) => element.id));
    assert.equal(new Set(ids).size, ids.length);
    assert.deepEqual(errors, []);
    assert.deepEqual(requests, []);
    console.log(
      "PASS: 4 apps, 6 load scenarios, 7 isolated capacity boundaries, 10 operations pages, role/stale/unknown/empty gates, private Auth exclusion, 3 upgrade scenarios and users>0 gate, 2 languages, 5 widths; no network calls.",
    );
  } finally {
    await browser.close();
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
