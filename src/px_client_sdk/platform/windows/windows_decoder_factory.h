#pragma once

#include <memory>

namespace px {
class VideoDecoderFactory;
struct WindowsVideoResources;
[[nodiscard]] std::shared_ptr<VideoDecoderFactory> MakeWindowsVideoDecoderFactory(std::shared_ptr<const WindowsVideoResources> resources);
} // namespace px
