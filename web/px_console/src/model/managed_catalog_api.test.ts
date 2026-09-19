import { beforeEach, describe, expect, it, vi } from "vitest";
import axiosHttp from "@/http";
import {
    createManagedApplication,
    replaceManagedApplicationGroups,
    type ApplicationSpec,
    type ManagedApplication,
} from "./managed_application_api";
import {
    configureManagedNode,
    createManagedNode,
    listManagedNodes,
    type ManagedNode,
} from "./managed_node_api";
import {
    configureManagedDeployment,
    createManagedDeployment,
    type DeploymentConfiguration,
    type ManagedDeployment,
} from "./managed_deployment_api";

vi.mock("@/http", () => ({
    default: {
        get: vi.fn(),
        post: vi.fn(),
        patch: vi.fn(),
        put: vi.fn(),
        delete: vi.fn(),
    },
}));

describe("PostgreSQL managed catalog API", () => {
    beforeEach(() => vi.clearAllMocks());

    it("uses the typed application spec and current revision for group grants", async () => {
        const spec: ApplicationSpec = {
            name: "Cloud IDE",
            access: "acl",
            launch: {
                kind: "webview",
                entry_url: "https://ide.example.test",
                video: { codec: "h264", bitrate_kbps: 20_000 },
            },
            allow_observer: false,
            allow_takeover: false,
            disabled: false,
        };
        const application: ManagedApplication = {
            id: "application-id",
            revision: 2,
            access_revision: 2,
            spec,
        };
        vi.mocked(axiosHttp.post).mockResolvedValue({ data: application } as never);
        vi.mocked(axiosHttp.put).mockResolvedValue({
            data: { ...application, revision: 3 },
        } as never);

        const created = await createManagedApplication(spec);
        await replaceManagedApplicationGroups(created, ["group-id"]);

        expect(axiosHttp.post).toHaveBeenCalledWith("/api/console/managed/applications", spec);
        expect(axiosHttp.put).toHaveBeenCalledWith(
            "/api/console/managed/applications/application-id/groups",
            {
                revision: 2,
                groups: ["group-id"],
            },
        );
    });

    it("creates a node credential then configures lifecycle state by revision", async () => {
        const node = {
            id: "node-id",
            device_id: "device-id",
            product: "cloud_node",
            revision: 4,
            max_instances: 4,
        } as ManagedNode;
        vi.mocked(axiosHttp.post).mockResolvedValue({
            data: { node, node_token: "secret" },
        } as never);
        vi.mocked(axiosHttp.patch).mockResolvedValue({ data: { ...node, revision: 5 } } as never);

        await createManagedNode("device-id", "cloud_node", 4);
        await configureManagedNode(node, { draining: true, disabled: false, max_instances: 4 });

        expect(axiosHttp.post).toHaveBeenCalledWith("/api/console/managed/nodes", {
            device_id: "device-id",
            product: "cloud_node",
            max_instances: 4,
        });
        expect(axiosHttp.patch).toHaveBeenCalledWith("/api/console/managed/nodes/node-id", {
            revision: 4,
            configuration: { draining: true, disabled: false, max_instances: 4 },
        });
    });

    it("retains explicit machine and per-GPU telemetry from the managed node view", async () => {
        const node = {
            id: "node-id",
            telemetry: {
                probe_state: "ready",
                cpu_utilization_per_mille: 375,
                gpu_inventory_revision: 7,
            },
            gpus: [
                {
                    stable_key: "pnp-sha256:0123456789abcdef",
                    name: "Synthetic GPU",
                    utilization_per_mille: null,
                },
            ],
        } as ManagedNode;
        vi.mocked(axiosHttp.get).mockResolvedValue({ data: [node] } as never);

        const nodes = await listManagedNodes();

        expect(nodes).toEqual([node]);
        const returnedNode = nodes.at(0);
        expect(returnedNode?.telemetry?.cpu_utilization_per_mille).toBe(375);
        expect(returnedNode?.gpus.at(0)?.stable_key).toBe("pnp-sha256:0123456789abcdef");
        expect(axiosHttp.get).toHaveBeenCalledWith("/api/console/managed/nodes", {
            params: { after: undefined, limit: 100 },
        });
    });

    it("keeps deployment target identity explicit on create and configure", async () => {
        const configuration: DeploymentConfiguration = {
            target: { kind: "game_hook", install_root: "D:\\Games\\Example" },
            gpu_key: "GPU-1:0",
            gpu_profile: {
                memory_bytes: 1073741824,
                compute_per_mille: 200,
                encoder_per_mille: 250,
                memory_reserve_bytes: 536870912,
                compute_limit_per_mille: 900,
                encoder_limit_per_mille: 900,
            },
            capacity: 2,
            disabled: false,
        };
        const deployment = {
            id: "deployment-id",
            application_id: "application-id",
            node_id: "node-id",
            kind: "game_hook",
            revision: 3,
        } as ManagedDeployment;
        vi.mocked(axiosHttp.post).mockResolvedValue({ data: deployment } as never);
        vi.mocked(axiosHttp.patch).mockResolvedValue({
            data: { ...deployment, revision: 4 },
        } as never);

        await createManagedDeployment("application-id", "node-id", configuration);
        await configureManagedDeployment(deployment, configuration);

        expect(axiosHttp.post).toHaveBeenCalledWith("/api/console/managed/deployments", {
            application_id: "application-id",
            node_id: "node-id",
            configuration,
        });
        expect(axiosHttp.patch).toHaveBeenCalledWith(
            "/api/console/managed/deployments/deployment-id",
            {
                revision: 3,
                configuration,
            },
        );
    });
});
