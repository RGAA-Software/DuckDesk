#include "client_file_transfer_format.h"

#include <cmath>
#include <format>

namespace px::client::imgui {

std::string FormatTransferSpeed(const double bytesPerSecond) {
    constexpr double kib{1024.0};
    constexpr double mib{kib * 1024.0};
    constexpr double gib{mib * 1024.0};
    const double speed{std::isfinite(bytesPerSecond) && bytesPerSecond > 0.0 ? bytesPerSecond : 0.0};
    if (speed >= gib)
        return std::format("{:.1f} GB/s", speed / gib);
    if (speed >= mib)
        return std::format("{:.1f} MB/s", speed / mib);
    if (speed >= kib)
        return std::format("{:.1f} KB/s", speed / kib);
    return std::format("{:.0f} B/s", speed);
}

} // namespace px::client::imgui
