"use strict";

const fs = require("node:fs");
const http = require("node:http");

const listenPort = Number.parseInt(process.argv[2], 10);
const resultPath = process.argv[3];

if (!Number.isInteger(listenPort) || listenPort < 1024 || listenPort > 65535 || !resultPath) {
    process.stderr.write("usage: node backup_alert_receiver.cjs <port> <result-path>\n");
    process.exit(2);
}

const receiver = http.createServer((request, response) => {
    if (request.method !== "POST" || request.url !== "/alerts") {
        response.writeHead(404);
        response.end();
        return;
    }

    const bodyChunks = [];
    request.on("data", bodyChunk => bodyChunks.push(bodyChunk));
    request.on("end", () => {
        try {
            const notification = JSON.parse(Buffer.concat(bodyChunks).toString("utf8"));
            const firingAlert = notification.alerts?.find(
                alert =>
                    alert.status === "firing" &&
                    alert.labels?.alertname === "PixelsBackupRepeatedFailures",
            );
            if (!firingAlert) {
                response.writeHead(202);
                response.end();
                return;
            }
            const acceptedResult = {
                alertName: firingAlert.labels.alertname,
                deploymentId: firingAlert.labels.deployment_id,
                status: firingAlert.status,
                receivedAt: new Date().toISOString(),
            };
            fs.writeFileSync(resultPath, `${JSON.stringify(acceptedResult, null, 2)}\n`, {
                encoding: "utf8",
                flag: "wx",
                mode: 0o600,
            });
            response.writeHead(204);
            response.end();
            receiver.close(() => process.exit(0));
        } catch (error) {
            process.stderr.write(`alert receiver rejected payload: ${error.message}\n`);
            response.writeHead(400);
            response.end();
        }
    });
});

receiver.listen(listenPort, "0.0.0.0", () => {
    process.stdout.write(`READY port=${listenPort}\n`);
});

setTimeout(() => {
    process.stderr.write("alert receiver timed out\n");
    receiver.close(() => process.exit(3));
}, 180_000).unref();
