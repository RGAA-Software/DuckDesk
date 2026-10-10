#pragma once

#include <Windows.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace px::transport::testing {
// Allows the same integration cases to run against an explicitly supplied private Relay.
inline std::string EndpointConfiguration() {
    const auto capacity = GetEnvironmentVariableA("PX_IROH_TEST_ENDPOINT_CONFIG", nullptr, 0);
    if (capacity == 0) return "{}";
    if (capacity > 65536) throw std::runtime_error("iroh test endpoint configuration is too large");
    std::vector<char> configuration(capacity);
    const auto length = GetEnvironmentVariableA("PX_IROH_TEST_ENDPOINT_CONFIG", configuration.data(), capacity);
    if (length == 0 || length >= capacity) throw std::runtime_error("iroh test endpoint configuration changed during read");
    return std::string(configuration.begin(), configuration.begin() + length);
}
}  // namespace px::transport::testing
