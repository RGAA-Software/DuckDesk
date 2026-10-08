import { effectScope } from "vue";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { useManagedRecordingPreview } from "./useManagedRecordingPreview";
import {
    downloadManagedRecording,
    listManagedRecordingCache,
    requestManagedRecordingCache,
    type RecordingProfile,
    type RecordingCacheProfile,
} from "@/model/managed_activity_api";

vi.mock("@/model/managed_activity_api", () => ({
    downloadManagedRecording: vi.fn(),
    listManagedRecordingCache: vi.fn(),
    requestManagedRecordingCache: vi.fn(),
}));
const recording: RecordingProfile = {
    id: "recording-1",
    node_id: "node-1",
    session_id: null,
    file_name: "demo.mp4",
    size_bytes: 10,
    modified_at: "2026-10-08T00:00:00Z",
    codec: "h264",
    reported_present: true,
    source_sequence: 1,
    revision: 1,
    created_at: "2026-10-08T00:00:00Z",
    observed_at: "2026-10-08T00:00:00Z",
};
function cache(state: RecordingCacheProfile["state"]): RecordingCacheProfile {
    return {
        recording_id: recording.id,
        state,
        pinned: false,
        revision: 1,
        size_bytes: 10,
        received_bytes: 5,
        updated_at: "2026-10-08T00:00:00Z",
    };
}
function fixture() {
    const scope = effectScope();
    const preview = scope.run(useManagedRecordingPreview)!;
    return { scope, preview };
}
const originalCreateUrl = URL.createObjectURL;
const originalRevokeUrl = URL.revokeObjectURL;
beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers();
    URL.createObjectURL = vi.fn().mockReturnValue("blob:recording-preview");
    URL.revokeObjectURL = vi.fn();
    vi.mocked(requestManagedRecordingCache).mockResolvedValue(cache("ready"));
    vi.mocked(downloadManagedRecording).mockResolvedValue(
        new Blob(["video"], { type: "video/mp4" }),
    );
});
afterEach(() => {
    vi.useRealTimers();
    URL.createObjectURL = originalCreateUrl;
    URL.revokeObjectURL = originalRevokeUrl;
});

describe("managed recording preview lifetime", () => {
    it("reads a published cache to trigger verification after a Console restart", async () => {
        const { scope, preview } = fixture();
        vi.mocked(requestManagedRecordingCache).mockResolvedValue(cache("verifying"));
        await preview.open(recording);
        expect(listManagedRecordingCache).not.toHaveBeenCalled();
        expect(downloadManagedRecording).toHaveBeenCalledWith(
            recording.id,
            expect.objectContaining({ signal: expect.any(AbortSignal) }),
        );
        expect(preview.phase.value).toBe("ready");
        scope.stop();
    });

    it("does not expose a cache when server-side verification fails", async () => {
        const { scope, preview } = fixture();
        vi.mocked(requestManagedRecordingCache).mockResolvedValue(cache("verifying"));
        vi.mocked(downloadManagedRecording).mockRejectedValueOnce({ response: { status: 409 } });
        await preview.open(recording);
        expect(preview.phase.value).toBe("error");
        expect(preview.errorKey.value).toBe("recordingPreview.failed");
        expect(URL.createObjectURL).not.toHaveBeenCalled();
        expect(listManagedRecordingCache).not.toHaveBeenCalled();
        scope.stop();
    });

    it("waits for a verified cache and releases playback when closed", async () => {
        const { scope, preview } = fixture();
        vi.mocked(requestManagedRecordingCache).mockResolvedValue(cache("fetching"));
        vi.mocked(listManagedRecordingCache).mockResolvedValue([cache("ready")]);
        const opening = preview.open(recording);
        await Promise.resolve();
        expect(preview.phase.value).toBe("preparing");
        expect(downloadManagedRecording).not.toHaveBeenCalled();
        await vi.advanceTimersByTimeAsync(1_000);
        await opening;
        expect(preview.phase.value).toBe("ready");
        expect(preview.source.value).toBe("blob:recording-preview");
        preview.close();
        expect(URL.revokeObjectURL).toHaveBeenCalledWith("blob:recording-preview");
        expect(preview.isOpen.value).toBe(false);
        scope.stop();
    });

    it("cancels a pending poll on destruction without another request", async () => {
        const { scope, preview } = fixture();
        vi.mocked(requestManagedRecordingCache).mockResolvedValue(cache("fetching"));
        const opening = preview.open(recording);
        await Promise.resolve();
        scope.stop();
        await opening;
        await vi.advanceTimersByTimeAsync(2_000);
        expect(listManagedRecordingCache).not.toHaveBeenCalled();
        expect(downloadManagedRecording).not.toHaveBeenCalled();
        expect(vi.getTimerCount()).toBe(0);
    });

    it("aborts loading and ignores a late response from a replaced recording", async () => {
        const { scope, preview } = fixture();
        let finishPrevious: (video: Blob) => void = () => {};
        vi.mocked(downloadManagedRecording).mockImplementationOnce(
            () =>
                new Promise(resolve => {
                    finishPrevious = resolve;
                }),
        );
        const previousOpening = preview.open(recording);
        await Promise.resolve();
        const previousSignal = vi.mocked(downloadManagedRecording).mock.calls[0]![1]!
            .signal as AbortSignal;
        await preview.open({ ...recording, id: "recording-2" });
        expect(previousSignal.aborted).toBe(true);
        finishPrevious(new Blob(["old-video"]));
        await previousOpening;
        expect(URL.createObjectURL).toHaveBeenCalledTimes(1);
        expect(preview.recording.value?.id).toBe("recording-2");
        scope.stop();
        expect(URL.revokeObjectURL).toHaveBeenCalledTimes(1);
    });

    it("stops polling when preparation fails and permits a fresh retry", async () => {
        const { scope, preview } = fixture();
        vi.mocked(requestManagedRecordingCache).mockResolvedValueOnce(cache("retry_required"));
        await preview.open(recording);
        expect(preview.errorKey.value).toBe("recordingPreview.unavailable");
        expect(preview.phase.value).toBe("error");
        expect(downloadManagedRecording).not.toHaveBeenCalled();
        await preview.open(recording);
        expect(preview.phase.value).toBe("ready");
        scope.stop();
    });

    it("reports revoked authorization and unsupported media without leaking a URL", async () => {
        const { scope, preview } = fixture();
        vi.mocked(downloadManagedRecording).mockRejectedValueOnce({ response: { status: 401 } });
        await preview.open(recording);
        expect(preview.phase.value).toBe("error");
        expect(preview.errorKey.value).toBe("recordingPreview.failed");
        expect(URL.createObjectURL).not.toHaveBeenCalled();
        await preview.open(recording);
        preview.playbackFailed();
        expect(preview.errorKey.value).toBe("recordingPreview.unsupported");
        expect(preview.source.value).toBe("");
        expect(URL.revokeObjectURL).toHaveBeenCalledTimes(1);
        scope.stop();
    });
});
