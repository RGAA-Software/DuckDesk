export const DIRECT_HOST_CONNECTION_TYPE = "rtc_direct";
export const DIRECT_HOST_UNREACHABLE_CODE = "RTC_DIRECT_UNREACHABLE";

export function rejectedDirectHostConnectionType(requestedConnectionType: string | null): string {
    const effectiveConnectionType = requestedConnectionType ?? DIRECT_HOST_CONNECTION_TYPE;
    return effectiveConnectionType === DIRECT_HOST_CONNECTION_TYPE ? "" : effectiveConnectionType;
}

export function createDirectHostRtcConfiguration(): RTCConfiguration {
    return { iceServers: [] };
}

export function directHostUnreachableMessage(): string {
    return `${DIRECT_HOST_UNREACHABLE_CODE}: 无法连接 Render Direct Host，请检查公网地址、端口和 UDP 防火墙`;
}
