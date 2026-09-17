import { describe, expect, it } from "vitest";
import { submissionIdentity } from "./submission";

describe("submission identity", () => {
    it("reuses unchanged retries, separates edits and resets after confirmed success", () => {
        const submission = submissionIdentity();
        const original = submission.next({ title: "same" });
        expect(submission.next({ title: "same" })).toBe(original);
        expect(submission.next({ title: "changed" })).not.toBe(original);
        const retry = submission.next({ title: "changed" });
        submission.reset();
        expect(submission.next({ title: "changed" })).not.toBe(retry);
    });
});
