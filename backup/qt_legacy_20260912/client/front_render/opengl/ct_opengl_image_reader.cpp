#include "ct_opengl_image_reader.h"
#include "px_client_sdk/gl/raw_image.h"

namespace px {
namespace {
std::shared_ptr<RawImage> ReadImage(const std::string& path, int width, int height, RawImageFormat format) {
    auto image = RawImage::Make(format, width, height);
    if (!image)
        return {};
    std::ifstream file(path, std::ios::binary);
    if (!file || !file.read(image->MutableBytes().data(), image->Size()))
        return {};
    return image;
}
} // namespace
std::shared_ptr<RawImage> ImageReader::ReadNV12(const std::string& path, int width, int height) {
    return ReadImage(path, width, height, kRawImageNV12);
}
std::shared_ptr<RawImage> ImageReader::ReadRGBA(const std::string& path, int width, int height) {
    return ReadImage(path, width, height, kRawImageRGBA);
}
std::shared_ptr<RawImage> ImageReader::ReadI420(const std::string& path, int width, int height) {
    return ReadImage(path, width, height, kRawImageI420);
}
} // namespace px
