import { describe, expect, it } from "vitest";
import {
    createDirectHostRtcConfiguration,
    DIRECT_HOST_CONNECTION_TYPE,
    DIRECT_HOST_UNREACHABLE_CODE,
    directHostUnreachableMessage,
    rejectedDirectHostConnectionType,
} from "../src/rtc/direct_host_policy";

describe("Direct Host connection policy", () => {
    it("uses rtc_direct when the descriptor omits a connection type", () => {
        expect(rejectedDirectHostConnectionType(null)).toBe("");
        expect(DIRECT_HOST_CONNECTION_TYPE).toBe("rtc_direct");
    });

    it("accepts only the exact Direct Host connection type", () => {
        expect(rejectedDirectHostConnectionType("rtc_direct")).toBe("");
        expect(rejectedDirectHostConnectionType("relay")).toBe("relay");
        expect(rejectedDirectHostConnectionType("rtc_direct ")).toBe("rtc_direct ");
    });

    it("creates an ICE configuration without STUN or TURN servers", () => {
        const firstConfiguration = createDirectHostRtcConfiguration();
        const secondConfiguration = createDirectHostRtcConfiguration();

        expect(firstConfiguration).toEqual({ iceServers: [] });
        expect(secondConfiguration).toEqual({ iceServers: [] });
        expect(secondConfiguration).not.toBe(firstConfiguration);
        expect(secondConfiguration.iceServers).not.toBe(firstConfiguration.iceServers);
    });

    it("reports a stable terminal error without advertising a fallback", () => {
        const message = directHostUnreachableMessage();

        expect(message).toContain(DIRECT_HOST_UNREACHABLE_CODE);
        expect(message).toContain("公网地址、端口和 UDP 防火墙");
        expect(message).not.toContain("fallback");
        expect(message).not.toContain("TURN");
        expect(message).not.toContain("Relay");
    });
});
