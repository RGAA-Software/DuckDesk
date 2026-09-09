#pragma once
#include <cstdint>

namespace px {
// Stable Windows process status contract shared with service_core/app_instance.rs.
// Ordinary exit code zero alone does not imply client-disconnect or idle timeout.
enum class ApplicationExitStatus : std::uint32_t {
    kUnspecified = 0,
    kNoClients = 0x47520001,
    kStartupIdle = 0x47520002,
};
} // namespace px
