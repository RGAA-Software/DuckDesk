import axiosHttp from "@/http";

export interface BackupTask {
    task_id: string;
    kind: "scheduled" | "manual";
    started_at_unix: number;
}

export interface BackupStatus {
    updated_at_unix: number;
    active_task: BackupTask | null;
    last_success_at_unix: number | null;
    last_recovery_set_id: string | null;
    last_failure_code: string | null;
    consecutive_failures: number;
    overdue: boolean;
}

export interface ManagedBackup {
    connected: boolean;
    status: BackupStatus | null;
    reported_at_unix: number | null;
}

export interface VerifiedRecoverySet {
    recovery_set_id: string;
    kind: string;
    status: string;
    created_at_unix: number;
    completed_at_unix: number | null;
    retention: string[];
    checked_at_unix: number;
}

export interface RecoverySetPreflight {
    recovery_set: VerifiedRecoverySet;
    repository_integrity: "verified_at_snapshot";
    restore_admission: "not_evaluated";
}

export async function getVerifiedRecoverySets(): Promise<VerifiedRecoverySet[]> {
    const response = await axiosHttp.get<{ recovery_sets: VerifiedRecoverySet[] }>("/api/console/managed/backup/recovery-sets");
    return response.data.recovery_sets;
}

export async function preflightRecoverySet(recoverySetId: string): Promise<RecoverySetPreflight> {
    const response = await axiosHttp.get<RecoverySetPreflight>(
        `/api/console/managed/backup/recovery-sets/${encodeURIComponent(recoverySetId)}/preflight`,
    );
    return response.data;
}

export async function getManagedBackup(): Promise<ManagedBackup> {
    const response = await axiosHttp.get<ManagedBackup>("/api/console/managed/backup");
    return response.data;
}

export async function triggerManagedBackup(): Promise<string> {
    const response = await axiosHttp.post<{ task_id: string }>("/api/console/managed/backup/trigger");
    return response.data.task_id;
}
