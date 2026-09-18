import { beforeEach, describe, expect, it, vi } from "vitest";
import { userResourceHttp } from "./http";
import { downloadRecording, getRecordingsPage, requestRecordingCache } from "./api";

vi.mock("./http", () => ({
    hasUserToken: vi.fn(),
    publicHttp: { post: vi.fn() },
    setUserToken: vi.fn(),
    userHttp: { get: vi.fn(), patch: vi.fn(), put: vi.fn(), delete: vi.fn() },
    userResourceHttp: { get: vi.fn(), post: vi.fn() },
}));

describe("user recording API", () => {
    beforeEach(() => vi.clearAllMocks());

    it("lists owned recordings without accepting a node authority from the browser", async () => {
        vi.mocked(userResourceHttp.get).mockResolvedValue({ data: [] } as never);

        await getRecordingsPage();

        expect(userResourceHttp.get).toHaveBeenCalledWith("/api/console/recordings", {
            params: { after: undefined, limit: 100 },
        });
    });

    it("requests an authorized cache before downloading bytes", async () => {
        vi.mocked(userResourceHttp.post).mockResolvedValue({
            data: { recording_id: "recording id", state: "ready" },
        } as never);
        vi.mocked(userResourceHttp.get).mockResolvedValue({ data: new Blob(["video"]) } as never);

        await requestRecordingCache("recording id");
        await downloadRecording("recording id");

        expect(userResourceHttp.post).toHaveBeenCalledWith(
            "/api/console/recordings/recording%20id/cache",
        );
        expect(userResourceHttp.get).toHaveBeenCalledWith(
            "/api/console/recordings/recording%20id/download",
            { responseType: "blob" },
        );
    });
});
