#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace px {
class RawImage;
}

namespace px::client::imgui {

struct ClientVideoFrame final {
    int width{};
    int height{};
    std::vector<std::uint8_t> bgra{};
    std::shared_ptr<px::RawImage> native{};
};

[[nodiscard]] std::shared_ptr<ClientVideoFrame> RetainVideoFrame(std::shared_ptr<px::RawImage> image);

} // namespace px::client::imgui
