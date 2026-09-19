import { createRequire } from "node:module";
import { createReadStream, existsSync, statSync } from "node:fs";
import { createServer } from "node:http";
import { extname, resolve, sep } from "node:path";

const require = createRequire(new URL("../web/px_console/package.json", import.meta.url));
const { chromium } = require("playwright");

const launchUrl = process.env.PIXELS_WEB_ACCEPTANCE_URL ?? "";
const timeoutMilliseconds = Number(process.env.PIXELS_WEB_ACCEPTANCE_TIMEOUT_MS ?? "60000");
const configuredBrowserPath = process.env.PIXELS_WEB_ACCEPTANCE_BROWSER ?? "";
const assetRoot = process.env.PIXELS_WEB_ACCEPTANCE_ASSET_ROOT ?? "";
const renderOrigin = process.env.PIXELS_WEB_ACCEPTANCE_RENDER_ORIGIN ?? "";
const browserExecutable = configuredBrowserPath ||
    (process.platform === "win32" ? "C:/Program Files/Google/Chrome/Application/chrome.exe" : "");

if (!launchUrl) {
    throw new Error("PIXELS_WEB_ACCEPTANCE_URL is required");
}
if (!Number.isInteger(timeoutMilliseconds) || timeoutMilliseconds < 15000 || timeoutMilliseconds > 180000) {
    throw new Error("PIXELS_WEB_ACCEPTANCE_TIMEOUT_MS must be between 15000 and 180000");
}
if (browserExecutable && !existsSync(browserExecutable)) {
    throw new Error(`Configured browser does not exist: ${browserExecutable}`);
}
if (assetRoot && (!existsSync(assetRoot) || !statSync(assetRoot).isDirectory())) {
    throw new Error(`Web acceptance asset root does not exist: ${assetRoot}`);
}
if (Boolean(assetRoot) !== Boolean(renderOrigin)) {
    throw new Error("Local asset mode requires both asset root and Render origin");
}

function contentType(filePath) {
    return new Map([
        [".css", "text/css; charset=utf-8"],
        [".html", "text/html; charset=utf-8"],
        [".js", "text/javascript; charset=utf-8"],
        [".png", "image/png"],
    ]).get(extname(filePath).toLowerCase()) ?? "application/octet-stream";
}

async function startLocalAssetServer() {
    if (!assetRoot) return { server: null, url: launchUrl };
    const root = resolve(assetRoot);
    const remoteOrigin = new URL(renderOrigin);
    const requestedLaunch = new URL(launchUrl);
    const server = createServer(async (request, response) => {
        try {
            const requestUrl = new URL(request.url ?? "/", "http://127.0.0.1");
            if (requestUrl.pathname.startsWith("/alloc/") || requestUrl.pathname.startsWith("/get/")) {
                const bodyParts = [];
                let bodySize = 0;
                for await (const part of request) {
                    bodySize += part.length;
                    if (bodySize > 2 * 1024 * 1024) throw new Error("Proxy request body exceeds 2 MiB");
                    bodyParts.push(part);
                }
                const forwardedHeaders = new Headers();
                for (const [name, value] of Object.entries(request.headers)) {
                    if (value && !["connection", "content-length", "host"].includes(name)) {
                        forwardedHeaders.set(name, Array.isArray(value) ? value.join(",") : value);
                    }
                }
                const remoteUrl = new URL(`${requestUrl.pathname}${requestUrl.search}`, remoteOrigin);
                const remoteResponse = await fetch(remoteUrl, {
                    method: request.method,
                    headers: forwardedHeaders,
                    body: bodyParts.length > 0 ? Buffer.concat(bodyParts) : undefined,
                });
                response.statusCode = remoteResponse.status;
                remoteResponse.headers.forEach((value, name) => response.setHeader(name, value));
                response.end(Buffer.from(await remoteResponse.arrayBuffer()));
                return;
            }

            const relativePath = requestUrl.pathname === "/web/" || requestUrl.pathname === "/web"
                ? "index.html"
                : decodeURIComponent(requestUrl.pathname.replace(/^\/web\//, ""));
            const filePath = resolve(root, relativePath);
            if (filePath !== root && !filePath.startsWith(`${root}${sep}`)) {
                response.writeHead(403).end();
                return;
            }
            if (!existsSync(filePath) || !statSync(filePath).isFile()) {
                response.writeHead(404).end();
                return;
            }
            response.setHeader("Content-Type", contentType(filePath));
            createReadStream(filePath).pipe(response);
        } catch (error) {
            response.statusCode = 502;
            response.end(error instanceof Error ? error.message : String(error));
        }
    });
    await new Promise((resolveListening, rejectListening) => {
        server.once("error", rejectListening);
        server.listen(0, "127.0.0.1", resolveListening);
    });
    const address = server.address();
    if (!address || typeof address === "string") throw new Error("Local asset server did not allocate a TCP port");
    const localLaunch = new URL(`http://127.0.0.1:${address.port}/web/`);
    localLaunch.search = requestedLaunch.search;
    localLaunch.hash = requestedLaunch.hash;
    return { server, url: localLaunch.toString() };
}

const browser = await chromium.launch({
    headless: true,
    executablePath: browserExecutable || undefined,
    args: ["--autoplay-policy=no-user-gesture-required"],
});
const localAssets = await startLocalAssetServer();

try {
    const context = await browser.newContext({ ignoreHTTPSErrors: true });
    const page = await context.newPage();
    const browserErrors = [];
    const browserMessages = [];
    const failedRequests = [];
    page.on("pageerror", (error) => browserErrors.push(error.message));
    page.on("console", (message) => {
        if (browserMessages.length < 40) browserMessages.push(message.text());
    });
    page.on("requestfailed", (request) => {
        if (failedRequests.length >= 20) return;
        const requestUrl = new URL(request.url());
        failedRequests.push(`${request.method()} ${requestUrl.origin}${requestUrl.pathname}: ${request.failure()?.errorText ?? "failed"}`);
    });

    await page.goto(localAssets.url, { waitUntil: "domcontentloaded", timeout: timeoutMilliseconds });
    try {
        await page.waitForFunction(
            () => window.__pc?.connectionState === "connected",
            undefined,
            { timeout: timeoutMilliseconds },
        );
    } catch (error) {
        const diagnostic = await page.evaluate(() => ({
            path: `${window.location.origin}${window.location.pathname}`,
            title: document.title,
            readyState: document.readyState,
            peerState: window.__pc?.connectionState ?? "missing",
            iceState: window.__pc?.iceConnectionState ?? "missing",
            bodyText: document.body?.innerText.slice(0, 1200) ?? "",
        }));
        throw new Error(JSON.stringify({
            message: error instanceof Error ? error.message : String(error),
            diagnostic,
            browserErrors,
            browserMessages,
            failedRequests,
        }).replaceAll(/[0-9a-f]{64}/g, "<redacted>"));
    }
    try {
        await page.waitForFunction(
            () => {
                const video = document.querySelector("video");
                if (!(video instanceof HTMLVideoElement) || video.videoWidth <= 0 || video.videoHeight <= 0) {
                    return false;
                }
                return video.getVideoPlaybackQuality().totalVideoFrames > 0;
            },
            undefined,
            { timeout: timeoutMilliseconds },
        );
    } catch (error) {
        const diagnostic = await page.evaluate(async () => {
            const peer = window.__pc;
            const inbound = [];
            if (peer) {
                const statistics = await peer.getStats();
                statistics.forEach((report) => {
                    if (report.type === "inbound-rtp") {
                        inbound.push({
                            kind: report.kind,
                            bytesReceived: report.bytesReceived ?? 0,
                            framesReceived: report.framesReceived ?? 0,
                            framesDecoded: report.framesDecoded ?? 0,
                            packetsReceived: report.packetsReceived ?? 0,
                        });
                    }
                });
            }
            const video = document.querySelector("video");
            return {
                peerState: peer?.connectionState ?? "missing",
                iceState: peer?.iceConnectionState ?? "missing",
                receivers: peer?.getReceivers().map((receiver) => ({
                    kind: receiver.track?.kind ?? "missing",
                    state: receiver.track?.readyState ?? "missing",
                    muted: receiver.track?.muted ?? true,
                })) ?? [],
                inbound,
                videoWidth: video instanceof HTMLVideoElement ? video.videoWidth : 0,
                videoHeight: video instanceof HTMLVideoElement ? video.videoHeight : 0,
                bodyText: document.body?.innerText.slice(0, 1200) ?? "",
            };
        });
        throw new Error(JSON.stringify({
            message: error instanceof Error ? error.message : String(error),
            diagnostic,
            browserErrors,
            browserMessages,
            failedRequests,
        }).replaceAll(/[0-9a-f]{64}/g, "<redacted>"));
    }

    const evidence = await page.evaluate(async () => {
        const peer = window.__pc;
        if (!peer) throw new Error("Web client did not expose its active peer connection");
        const statistics = await peer.getStats();
        let videoBytes = 0;
        let videoFrames = 0;
        let audioBytes = 0;
        statistics.forEach((report) => {
            if (report.type !== "inbound-rtp") return;
            if (report.kind === "video") {
                videoBytes += Number(report.bytesReceived ?? 0);
                videoFrames += Number(report.framesDecoded ?? 0);
            } else if (report.kind === "audio") {
                audioBytes += Number(report.bytesReceived ?? 0);
            }
        });
        const video = document.querySelector("video");
        const playback = video instanceof HTMLVideoElement ? video.getVideoPlaybackQuality() : null;
        return {
            connectionState: peer.connectionState,
            iceConnectionState: peer.iceConnectionState,
            signalingState: peer.signalingState,
            sctpState: peer.sctp?.state ?? "unavailable",
            videoWidth: video instanceof HTMLVideoElement ? video.videoWidth : 0,
            videoHeight: video instanceof HTMLVideoElement ? video.videoHeight : 0,
            playbackFrames: playback?.totalVideoFrames ?? 0,
            videoBytes,
            videoFrames,
            audioBytes,
            visibleUrlContainsFrontendToken: window.location.href.includes("frontend_token="),
        };
    });

    if (evidence.connectionState !== "connected" || evidence.videoBytes <= 0 || evidence.videoFrames <= 0) {
        throw new Error(`WebRTC media evidence is incomplete: ${JSON.stringify(evidence)}`);
    }
    if (evidence.visibleUrlContainsFrontendToken) {
        throw new Error("The visible browser URL still contains the frontend token");
    }
    if (browserErrors.length > 0) {
        throw new Error(`Browser page errors: ${browserErrors.join(" | ")}`);
    }

    process.stdout.write(`${JSON.stringify({ result: "PASS", ...evidence })}\n`);
    await context.close();
} finally {
    await browser.close();
    if (localAssets.server) {
        await new Promise((resolveClose, rejectClose) => {
            localAssets.server.close((error) => error ? rejectClose(error) : resolveClose());
        });
    }
}
