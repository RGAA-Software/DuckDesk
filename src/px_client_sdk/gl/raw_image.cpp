#include "raw_image.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <fstream>
namespace px {
std::shared_ptr<RawImage> RawImage::Make(RawImageFormat format, int width, int height, std::span<const char> source) {
    if (width <= 0 || height <= 0)
        return {};
    auto image = std::make_shared<RawImage>();
    image->img_width = width;
    image->img_height = height;
    image->img_format = format;
    const auto add_plane = [image](std::uint64_t stride, int rows) {
        const auto bytes = stride * static_cast<std::uint64_t>(rows);
        const auto previous = image->plane_count_ == 0 ? ImagePlane{} : image->planes_[image->plane_count_ - 1];
        const auto offset = previous.offset + previous.size;
        if (stride > INT_MAX || bytes > INT_MAX || offset > static_cast<std::size_t>(INT_MAX) - bytes)
            return false;
        image->planes_[image->plane_count_++] = {offset, static_cast<std::size_t>(bytes), static_cast<int>(stride), rows};
        return true;
    };
    const auto chroma_width = static_cast<std::uint64_t>(width / 2 + width % 2);
    const auto chroma_height = height / 2 + height % 2;
    bool valid{};
    switch (format) {
    case kRawImageRGB:
    case kRawImageRGBA:
        image->img_ch = format == kRawImageRGB ? 3 : 4;
        valid = add_plane(static_cast<std::uint64_t>(width) * image->img_ch, height);
        break;
    case kRawImageNV12:
        valid = add_plane(width, height) && add_plane(chroma_width * 2, chroma_height);
        break;
    case kRawImageI420:
        valid = add_plane(width, height) && add_plane(chroma_width, chroma_height) && add_plane(chroma_width, chroma_height);
        break;
    case kRawImageI444:
        valid = add_plane(width, height) && add_plane(width, height) && add_plane(width, height);
        image->full_color_ = true;
        break;
    default:
        return {};
    }
    if (!valid)
        return {};
    const auto last = image->planes_[image->plane_count_ - 1];
    const auto size = last.offset + last.size;
    if (!source.empty() && source.size() != size)
        return {};
    image->buffer_.resize(size);
    if (!source.empty())
        std::ranges::copy(source, image->buffer_.begin());
    return image;
}
std::shared_ptr<RawImage> RawImage::MakePresented(int width, int height) {
    if (width <= 0 || height <= 0)
        return {};
    auto image = std::make_shared<RawImage>();
    image->img_width = width;
    image->img_height = height;
    image->img_format = kRawImagePresented;
    return image;
}
std::shared_ptr<RawImage> RawImage::MakePlatform(RawImageFormat format, int width, int height, std::shared_ptr<PlatformImage> storage) {
    if (!storage || (format != kRawImageD3D11Texture && format != kRawImageVulkanAVFrame))
        return {};
    auto image = MakePresented(width, height);
    if (!image)
        return {};
    image->img_format = format;
    image->platform_ = std::move(storage);
    return image;
}
std::optional<ImagePlane> RawImage::Layout(std::size_t index) const noexcept {
    return index < plane_count_ ? std::optional{planes_[index]} : std::nullopt;
}
std::span<const char> RawImage::Plane(std::size_t index) const noexcept {
    const auto plane = Layout(index);
    return plane ? Bytes().subspan(plane->offset, plane->size) : std::span<const char>{};
}
std::span<char> RawImage::MutablePlane(std::size_t index) noexcept {
    const auto plane = Layout(index);
    return plane ? MutableBytes().subspan(plane->offset, plane->size) : std::span<char>{};
}
std::shared_ptr<RawImage> RawImage::Clone() const {
    auto copy = std::make_shared<RawImage>(*this);
    if (platform_) {
        copy->platform_ = platform_->Clone();
        if (!copy->platform_)
            return {};
    }
    return copy;
}
void RawImage::CopyTo(const std::shared_ptr<RawImage>& target) const {
    if (!target || target.get() == this || platform_ || target->platform_ || img_format != target->img_format || img_width != target->img_width ||
        img_height != target->img_height || buffer_.size() != target->buffer_.size())
        return;
    std::ranges::copy(buffer_, target->buffer_.begin());
    target->full_color_ = full_color_;
}
void RawImage::SaveYUV444ToFile(const std::string& filename) const {
    if (img_format != kRawImageI444 || buffer_.empty())
        return;
    std::ofstream file(filename, std::ios::binary | std::ios::trunc);
    if (file)
        file.write(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
}
void RawImage::AppendYUV444ToFile(const std::string& filename) const {
    if (img_format != kRawImageI444 || buffer_.empty())
        return;
    std::ofstream file(filename, std::ios::binary | std::ios::app);
    if (file)
        file.write(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
}
} // namespace px
