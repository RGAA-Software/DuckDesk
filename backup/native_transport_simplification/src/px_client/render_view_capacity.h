#pragma once

#include <algorithm>
#include <cstddef>

#include "ct_const_def.h"

namespace px
{
    inline std::size_t NormalizeRenderViewLimit(int configured_limit) {
        return static_cast<std::size_t>(
            std::clamp(configured_limit, 1, kMaxRenderViewCount));
    }

    inline std::size_t ResolveRequiredRenderViewCount(
        int requested_count,
        int configured_limit) {
        const auto limit = NormalizeRenderViewLimit(configured_limit);
        if (requested_count <= 1) {
            return 1;
        }
        return std::min(static_cast<std::size_t>(requested_count), limit);
    }
}
