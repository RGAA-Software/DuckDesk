#include "client_video_frame.h"

#include "px_client_sdk/gl/raw_image.h"

#include <utility>

namespace px::client::imgui {

std::shared_ptr<ClientVideoFrame> RetainVideoFrame(std::shared_ptr<px::RawImage> image) {
    if (!image || image->img_width <= 0 || image->img_height <= 0)
        return {};
    return std::make_shared<ClientVideoFrame>(ClientVideoFrame{.width = image->img_width, .height = image->img_height, .native = std::move(image)});
}

} // namespace px::client::imgui
