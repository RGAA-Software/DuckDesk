import { beforeEach, describe, expect, it, vi } from "vitest";
import axiosHttp from "@/http";
import {
    acknowledgeTelemetryAlert,
    configureTelemetryAlertPolicy,
    getTelemetryAlert,
    getTelemetryAlertPolicy,
    listTelemetryAlerts,
    type TelemetryAlertEvent,
} from "./telemetry_alert_api";

vi.mock("@/http", () => ({ default: { get: vi.fn(), patch: vi.fn() } }));

const alert: TelemetryAlertEvent = {
    id: "alert-id",
    node_id: "node-id",
    metric: "cpu",
    resource_key: "cpu",
    resource_name: "CPU",
    severity: "critical",
    state: "active",
    threshold_per_mille: 950,
    first_value_per_mille: 960,
    latest_value_per_mille: 970,
    peak_value_per_mille: 980,
    occurrence_count: 3,
    first_sampled_at: "2026-09-18T10:00:00Z",
    last_sampled_at: "2026-09-18T10:01:00Z",
    created_at: "2026-09-18T10:00:00Z",
    updated_at: "2026-09-18T10:01:00Z",
    acknowledged_by: null,
    acknowledged_at: null,
    recovered_at: null,
    revision: 2,
};

describe("PostgreSQL telemetry alert API", () => {
    beforeEach(() => vi.clearAllMocks());

    it("uses a complete stable cursor and explicit filters", async () => {
        vi.mocked(axiosHttp.get).mockResolvedValue({ data: [alert] } as never);
        await expect(
            listTelemetryAlerts(
                { nodeId: alert.node_id, metric: "cpu", severity: "critical", state: "active" },
                25,
                alert,
            ),
        ).resolves.toEqual([alert]);
        expect(axiosHttp.get).toHaveBeenCalledWith("/api/console/managed/telemetry-alerts", {
            params: {
                node_id: alert.node_id,
                metric: "cpu",
                severity: "critical",
                state: "active",
                before_updated_at: alert.updated_at,
                before_id: alert.id,
                limit: 25,
            },
        });
    });

    it("loads details and acknowledges with the event revision", async () => {
        vi.mocked(axiosHttp.get).mockResolvedValue({ data: alert } as never);
        vi.mocked(axiosHttp.patch).mockResolvedValue({
            data: { ...alert, state: "acknowledged", revision: 3 },
        } as never);
        await expect(getTelemetryAlert(alert.id)).resolves.toEqual(alert);
        await acknowledgeTelemetryAlert(alert);
        expect(axiosHttp.patch).toHaveBeenCalledWith(
            "/api/console/managed/telemetry-alerts/alert-id/acknowledgement",
            { revision: 2 },
        );
    });

    it("loads and configures an explicit per-node policy", async () => {
        const policy = {
            node_id: alert.node_id,
            revision: 3,
            cpu_warning_per_mille: 850,
            cpu_critical_per_mille: 950,
            memory_warning_per_mille: 850,
            memory_critical_per_mille: 950,
            disk_warning_per_mille: 850,
            disk_critical_per_mille: 950,
            gpu_warning_per_mille: 900,
            gpu_critical_per_mille: 980,
            trigger_samples: 3,
            recovery_samples: 3,
            recovery_hysteresis_per_mille: 50,
            updated_at: alert.updated_at,
        };
        vi.mocked(axiosHttp.get).mockResolvedValue({ data: policy } as never);
        vi.mocked(axiosHttp.patch).mockResolvedValue({ data: { ...policy, revision: 4 } } as never);
        await expect(getTelemetryAlertPolicy(alert.node_id)).resolves.toEqual(policy);
        await configureTelemetryAlertPolicy(policy);
        expect(axiosHttp.patch).toHaveBeenCalledWith(
            "/api/console/managed/nodes/node-id/telemetry-alert-policy",
            expect.objectContaining({ revision: 3, trigger_samples: 3 }),
        );
    });
});
