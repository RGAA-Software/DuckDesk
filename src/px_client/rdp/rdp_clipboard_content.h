#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace px::rdp {

class ClipboardStagingDirectory;

struct ClipboardContent final {
    std::string text{};
    std::vector<std::uint8_t> html{};
    std::vector<std::uint8_t> dib{};
    std::vector<std::uint8_t> dibV5{};
    std::vector<std::uint8_t> png{};
    std::vector<std::filesystem::path> files{};
    std::shared_ptr<ClipboardStagingDirectory> staging{};

    [[nodiscard]] bool Empty() const noexcept { return text.empty() && html.empty() && dib.empty() && dibV5.empty() && png.empty() && files.empty(); }

    bool operator==(const ClipboardContent&) const = default;
};

}  // namespace px::rdp
