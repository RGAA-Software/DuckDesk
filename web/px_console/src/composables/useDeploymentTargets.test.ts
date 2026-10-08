import { effectScope } from "vue";
import { beforeEach, describe, expect, it, vi } from "vitest";
import { useDeploymentTargets } from "./useDeploymentTargets";
import {
    configureManagedDeployment,
    createManagedDeployment,
    listManagedDeployments,
    type ManagedDeployment,
} from "@/model/managed_deployment_api";
vi.mock("@/model/managed_deployment_api", () => ({
    configureManagedDeployment: vi.fn(),
    createManagedDeployment: vi.fn(),
    listManagedDeployments: vi.fn(),
}));
const original = {
    id: "deployment-old",
    application_id: "heart",
    node_id: "node-old",
    kind: "game_hook",
    gpu_key: "gpu-old",
    capacity: 12,
    disabled: false,
    revision: 3,
} as ManagedDeployment;
function fixture(initial = [original], kind: "game_hook" | "rdp" = "game_hook") {
    const scope = effectScope();
    return { scope, targets: scope.run(() => useDeploymentTargets("heart", kind, initial))! };
}
beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(listManagedDeployments).mockResolvedValue([original]);
});
describe("deployment machine set", () => {
    it("creates all selected new nodes and preserves existing settings", async () => {
        const { scope, targets } = fixture();
        targets.selectedNodeIds.value = ["node-old", "node-new-1", "node-new-2"];
        targets.newNodeCapacity.value = 4;
        await targets.save();
        expect(createManagedDeployment).toHaveBeenCalledTimes(2);
        for (const nodeId of ["node-new-1", "node-new-2"])
            expect(createManagedDeployment).toHaveBeenCalledWith("heart", nodeId, {
                target: { kind: "game_hook" },
                capacity: 4,
                gpu_key: null,
                disabled: false,
            });
        expect(configureManagedDeployment).not.toHaveBeenCalled();
        expect(targets.complete.value).toBe(true);
        scope.stop();
    });
    it("adds replacements before disabling removed nodes and preserves per-node settings", async () => {
        const { scope, targets } = fixture();
        targets.selectedNodeIds.value = ["node-new"];
        await targets.save();
        expect(configureManagedDeployment).toHaveBeenCalledWith(original, {
            target: { kind: "game_hook" },
            capacity: 12,
            gpu_key: "gpu-old",
            disabled: true,
        });
        expect(vi.mocked(createManagedDeployment).mock.invocationCallOrder[0]!).toBeLessThan(
            vi.mocked(configureManagedDeployment).mock.invocationCallOrder[0]!,
        );
        scope.stop();
    });
    it("keeps old nodes if additions fail and retries only unfinished work", async () => {
        const { scope, targets } = fixture();
        targets.selectedNodeIds.value = ["node-new-1", "node-new-2"];
        vi.mocked(createManagedDeployment)
            .mockResolvedValueOnce({ ...original, id: "new-1", node_id: "node-new-1" })
            .mockRejectedValueOnce(new Error("offline"));
        await targets.save();
        expect(configureManagedDeployment).not.toHaveBeenCalled();
        expect(targets.results.value.map(result => result.status)).toEqual([
            "saved",
            "error",
            "blocked",
        ]);
        vi.mocked(listManagedDeployments).mockResolvedValue([
            original,
            { ...original, id: "new-1", node_id: "node-new-1" },
        ]);
        await targets.save();
        expect(createManagedDeployment).toHaveBeenCalledTimes(3);
        expect(targets.complete.value).toBe(true);
        expect(configureManagedDeployment).toHaveBeenCalledTimes(1);
        scope.stop();
    });
    it("reenables a selected existing deployment without changing capacity or GPU", async () => {
        const disabled = { ...original, disabled: true };
        vi.mocked(listManagedDeployments).mockResolvedValue([disabled]);
        const { scope, targets } = fixture([disabled]);
        expect(targets.selectedNodeIds.value).toEqual([]);
        targets.selectedNodeIds.value = ["node-old"];
        await targets.save();
        expect(createManagedDeployment).not.toHaveBeenCalled();
        expect(configureManagedDeployment).toHaveBeenCalledWith(disabled, {
            target: { kind: "game_hook" },
            capacity: 12,
            gpu_key: "gpu-old",
            disabled: false,
        });
        scope.stop();
    });
    it("does not touch other applications and uses one slot for each RDP node", async () => {
        vi.mocked(listManagedDeployments).mockResolvedValue([
            { ...original, application_id: "other" },
        ]);
        const { scope, targets } = fixture([], "rdp");
        targets.selectedNodeIds.value = ["node-new-1", "node-new-2"];
        targets.newNodeCapacity.value = 9;
        await targets.save();
        expect(configureManagedDeployment).not.toHaveBeenCalled();
        expect(createManagedDeployment).toHaveBeenCalledWith("heart", "node-new-1", {
            target: { kind: "rdp" },
            capacity: 1,
            gpu_key: null,
            disabled: false,
        });
        scope.stop();
    });
    it("does not create duplicates after an ambiguous request failure", async () => {
        const { scope, targets } = fixture();
        targets.selectedNodeIds.value = ["node-old", "node-new"];
        vi.mocked(createManagedDeployment).mockRejectedValueOnce({
            isAxiosError: true,
            response: { status: 409 },
        });
        await targets.save();
        vi.mocked(listManagedDeployments).mockResolvedValue([
            original,
            { ...original, id: "new", node_id: "node-new" },
        ]);
        await targets.save();
        expect(createManagedDeployment).toHaveBeenCalledTimes(1);
        expect(targets.complete.value).toBe(true);
        scope.stop();
    });
    it("stops queued saves on disposal and suppresses duplicate clicks", async () => {
        const { scope, targets } = fixture();
        targets.selectedNodeIds.value = ["node-new-1", "node-new-2"];
        let finishSave: (deployment: ManagedDeployment) => void = () => {};
        vi.mocked(createManagedDeployment).mockImplementationOnce(
            () =>
                new Promise(resolve => {
                    finishSave = resolve;
                }),
        );
        const saving = targets.save();
        await Promise.resolve();
        await targets.save();
        expect(createManagedDeployment).toHaveBeenCalledTimes(1);
        scope.stop();
        finishSave(original);
        await saving;
        expect(createManagedDeployment).toHaveBeenCalledTimes(1);
        expect(configureManagedDeployment).not.toHaveBeenCalled();
    });
});
