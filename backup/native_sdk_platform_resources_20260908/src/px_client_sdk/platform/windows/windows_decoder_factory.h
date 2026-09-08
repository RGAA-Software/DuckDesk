#pragma once

#include <memory>

namespace px {
class VideoDecoderFactory;
[[nodiscard]] std::shared_ptr<VideoDecoderFactory> MakeWindowsVideoDecoderFactory();
} // namespace px
