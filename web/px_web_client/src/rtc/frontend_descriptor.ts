export interface FrontendDescriptor {
    sessionId: string;
    sessionRevision: string;
    token: string;
    consoleOrigin: string;
}

export interface ParsedFrontendDescriptor {
    descriptor: FrontendDescriptor | null;
    incomplete: boolean;
    removedToken: boolean;
}

export function takeFrontendDescriptor(fragment: URLSearchParams): ParsedFrontendDescriptor {
    const descriptor: FrontendDescriptor = {
        sessionId: fragment.get("session_id") ?? "",
        sessionRevision: fragment.get("session_revision") ?? "",
        token: fragment.get("frontend_token") ?? "",
        consoleOrigin: fragment.get("console_origin") ?? "",
    };
    const fields = [descriptor.sessionId, descriptor.sessionRevision, descriptor.token];
    const incomplete = fields.some(Boolean) && !fields.every(Boolean);
    const removedToken = Boolean(descriptor.token);
    if (removedToken) fragment.delete("frontend_token");
    return {
        descriptor: incomplete || !fields.every(Boolean) ? null : descriptor,
        incomplete,
        removedToken,
    };
}

export function appendFrontendAuthorization(
    query: URLSearchParams,
    descriptor: FrontendDescriptor | null,
    passwordHash: string,
    descriptorIncomplete = false,
) {
    if (descriptorIncomplete) {
        throw new Error("Console frontend descriptor is incomplete");
    }
    if (!descriptor) {
        query.set("safety_pwd_md5", passwordHash);
        return;
    }
    query.set("session_id", descriptor.sessionId);
    query.set("session_revision", descriptor.sessionRevision);
    query.set("frontend_token", descriptor.token);
}
