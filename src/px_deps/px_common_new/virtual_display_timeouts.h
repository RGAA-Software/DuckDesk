#pragma once

#include <chrono>

namespace px {

    // These budgets are deliberately layered. The Service owns the driver
    // operation, Render waits longer than the Service, and the client waits
    // for both the Service result and a possible capture rebuild.
    inline constexpr auto kVirtualDisplayQueryServiceTimeout = std::chrono::seconds(15);
    inline constexpr auto kVirtualDisplayMutationServiceTimeout = std::chrono::seconds(90);
    inline constexpr auto kVirtualDisplayResetServiceTimeout = std::chrono::seconds(600);

    inline constexpr auto kVirtualDisplayQueryRenderTimeout = std::chrono::seconds(25);
    inline constexpr auto kVirtualDisplayMutationRenderTimeout = std::chrono::seconds(100);
    inline constexpr auto kVirtualDisplayResetRenderTimeout = std::chrono::seconds(620);
    inline constexpr auto kVirtualDisplayCaptureRebuildTimeout = std::chrono::seconds(60);

    inline constexpr auto kVirtualDisplayClientOperationTimeout = std::chrono::seconds(170);

    static_assert(kVirtualDisplayQueryRenderTimeout > kVirtualDisplayQueryServiceTimeout);
    static_assert(kVirtualDisplayMutationRenderTimeout > kVirtualDisplayMutationServiceTimeout);
    static_assert(kVirtualDisplayResetRenderTimeout > kVirtualDisplayResetServiceTimeout);
    static_assert(kVirtualDisplayClientOperationTimeout >
                  kVirtualDisplayMutationRenderTimeout + kVirtualDisplayCaptureRebuildTimeout);

}
