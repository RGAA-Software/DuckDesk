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
};

[[nodiscard]] std::shared_ptr<ClientVideoFrame> ConvertVideoFrame(const std::shared_ptr<px::RawImage>& image);

} // namespace px::client::imgui
