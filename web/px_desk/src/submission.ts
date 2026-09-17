// Keep the request ID when retrying an unchanged form after an uncertain network result.
// A changed form starts a new submission; no content or credentials are persisted.
export function submissionIdentity() {
    let previous = "";
    let id = "";
    return {
        next(body: object) {
            const serialized = JSON.stringify(body);
            if (serialized !== previous) {
                previous = serialized;
                id = crypto.randomUUID();
            }
            return id;
        },
        reset() {
            previous = "";
            id = "";
        },
    };
}
