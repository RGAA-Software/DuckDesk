#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace px::gpu {

[[nodiscard]] std::int64_t PackAdapterLuid(std::uint32_t low_part, std::int32_t high_part);
[[nodiscard]] std::vector<std::string> StableKeysForAdapter(std::int64_t packed_luid);
[[nodiscard]] bool AdapterMatchesStableKey(std::int64_t packed_luid, std::string_view expected_stable_key);

}  // namespace px::gpu
