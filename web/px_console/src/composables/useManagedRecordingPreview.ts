import { onScopeDispose, ref } from "vue";
import {
    downloadManagedRecording,
    listManagedRecordingCache,
    requestManagedRecordingCache,
    type RecordingProfile,
} from "@/model/managed_activity_api";

type PreviewPhase = "idle" | "preparing" | "loading" | "ready" | "error";
type PreviewErrorKey =
    | "recordingPreview.unavailable"
    | "recordingPreview.failed"
    | "recordingPreview.unsupported";

function waitForCache(signal: AbortSignal): Promise<void> {
    return new Promise((resolve, reject) => {
        if (signal.aborted) {
            reject(new DOMException("Aborted", "AbortError"));
            return;
        }
        const timer = window.setTimeout(() => {
            signal.removeEventListener("abort", abort);
            resolve();
        }, 1_000);
        function abort() {
            window.clearTimeout(timer);
            signal.removeEventListener("abort", abort);
            reject(new DOMException("Aborted", "AbortError"));
        }
        signal.addEventListener("abort", abort, { once: true });
    });
}

export function useManagedRecordingPreview() {
    const recording = ref<RecordingProfile>();
    const phase = ref<PreviewPhase>("idle");
    const progress = ref(0);
    const source = ref("");
    const errorKey = ref<PreviewErrorKey>("recordingPreview.failed");
    const isOpen = ref(false);
    let operation: AbortController | undefined;

    function release() {
        operation?.abort();
        operation = undefined;
        if (source.value) URL.revokeObjectURL(source.value);
        source.value = "";
        progress.value = 0;
    }

    function close() {
        release();
        phase.value = "idle";
        isOpen.value = false;
        recording.value = undefined;
    }

    async function open(selectedRecording: RecordingProfile) {
        release();
        recording.value = selectedRecording;
        isOpen.value = true;
        phase.value = "preparing";
        const currentOperation = new AbortController();
        operation = currentOperation;
        const { signal } = currentOperation;
        try {
            let cache = await requestManagedRecordingCache(selectedRecording.id, signal);
            // Reading a published cache performs server-side verification after a restart.
            while (!signal.aborted && cache.state !== "ready" && cache.state !== "verifying") {
                if (cache.state === "missing" || cache.state === "retry_required") {
                    errorKey.value = "recordingPreview.unavailable";
                    phase.value = "error";
                    return;
                }
                progress.value =
                    cache.size_bytes > 0
                        ? Math.min(100, Math.floor((100 * cache.received_bytes) / cache.size_bytes))
                        : 0;
                await waitForCache(signal);
                const profiles = await listManagedRecordingCache(signal);
                if (signal.aborted) return;
                const currentCache = profiles.find(
                    profile => profile.recording_id === selectedRecording.id,
                );
                if (!currentCache) {
                    errorKey.value = "recordingPreview.unavailable";
                    phase.value = "error";
                    return;
                }
                cache = currentCache;
            }
            if (signal.aborted) return;
            phase.value = "loading";
            progress.value = 0;
            const video = await downloadManagedRecording(selectedRecording.id, {
                signal,
                timeout: 0,
                onDownloadProgress: transfer => {
                    if (signal.aborted) return;
                    const total = transfer.total || selectedRecording.size_bytes;
                    progress.value =
                        total > 0 ? Math.min(100, Math.floor((100 * transfer.loaded) / total)) : 0;
                },
            });
            if (signal.aborted) return;
            if (video.size === 0) throw new Error("Empty recording");
            source.value = URL.createObjectURL(video);
            progress.value = 100;
            phase.value = "ready";
        } catch {
            if (signal.aborted) return;
            errorKey.value = "recordingPreview.failed";
            phase.value = "error";
        }
    }

    function playbackFailed() {
        if (phase.value !== "ready") return;
        release();
        errorKey.value = "recordingPreview.unsupported";
        phase.value = "error";
    }

    onScopeDispose(close);
    return { recording, phase, progress, source, errorKey, isOpen, open, close, playbackFailed };
}
