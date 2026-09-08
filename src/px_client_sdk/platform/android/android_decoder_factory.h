#pragma once

#include <memory>

namespace px {
class VideoDecoderFactory;
class AndroidVideoOutput;
[[nodiscard]] std::shared_ptr<VideoDecoderFactory> MakeAndroidVideoDecoderFactory(std::shared_ptr<AndroidVideoOutput> output, bool prefer_software);
} // namespace px
