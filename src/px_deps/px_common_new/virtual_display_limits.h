#ifndef PX_COMMON_NEW_VIRTUAL_DISPLAY_LIMITS_H
#define PX_COMMON_NEW_VIRTUAL_DISPLAY_LIMITS_H

#include <cstdint>

namespace px
{
    // Keep the product capacity aligned with the eight negotiated RTC monitor
    // tracks. Displays are still created on demand; this is a limit, not a
    // preallocation count.
    inline constexpr std::uint32_t kVirtualDisplayMaximumCount = 8;
}

#endif // PX_COMMON_NEW_VIRTUAL_DISPLAY_LIMITS_H
