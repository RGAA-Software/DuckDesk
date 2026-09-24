const MAX_STATUS_SNAPSHOT_AGE_MS = 30_000;

export function managementSnapshotCurrent(
    refreshedAtMs: number | undefined,
    observedAtMs: number,
): boolean {
    if (refreshedAtMs === undefined) return false;
    const ageMs = observedAtMs - refreshedAtMs;
    return Number.isFinite(ageMs) && ageMs >= 0 && ageMs <= MAX_STATUS_SNAPSHOT_AGE_MS;
}
