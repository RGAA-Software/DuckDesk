#include "image.h"
#include "log.h"

#ifdef WIN32
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#endif

namespace px {
std::shared_ptr<Image> Image::Make(const DataPtr& data, int width, int height, int channels) {
    return std::make_shared<Image>(data, width, height, channels);
}

std::shared_ptr<Image> Image::Make(const DataPtr& data, int width, int height) {
    int channels = 0;
    if (data && width > 0 && height > 0) {
        channels = static_cast<int>(data->Size() / width / height);
    }
    return std::make_shared<Image>(data, width, height, channels);
}

std::shared_ptr<Image> Image::Make(const DataPtr& data, int width, int height, const RawImageType& rt) {
    auto image = Image::Make(data, width, height);
    image->raw_img_type_ = rt;
    return image;
}

#ifdef WIN32
std::shared_ptr<Image> Image::MakeByCompressedImage(const DataPtr& data) {
    return std::make_shared<Image>(data);
}
#endif

Image::Image(const DataPtr& data, int width, int height, int channels) {
    this->data = data;
    this->width = width;
    this->height = height;
    this->channels = channels;
}

#ifdef WIN32
Image::Image(const DataPtr& img_data) {
    if (!img_data || img_data->Size() <= 0) {
        LOGE("event=image.decode component=common_image code=EMPTY_IMAGE_DATA operation=decode "
             "outcome=failed recoverable=false");
        return;
    }
    using StbImageBuffer = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    StbImageBuffer buffer(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(img_data->Bytes().data()),
                                                static_cast<int>(img_data->Size()), &width, &height, &channels, 0),
                          &stbi_image_free);
    if (!buffer) {
        LOGE("stbi load image failed !");
        return;
    }
    const auto decoded_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels);
    data = Data::Copy(std::span<const char>{reinterpret_cast<const char*>(buffer.get()), decoded_size});
}
#endif

Image::~Image() {}

int Image::GetWidth() const {
    return width;
}

int Image::GetHeight() const {
    return height;
}

int Image::GetChannels() const {
    return channels;
}

DataPtr Image::GetData() const {
    return data;
}

int Image::GetInternalFormat() const {
    // #define GL_RGB                            0x1907
    // #define GL_RGBA                           0x1908
    if (channels == 3) {
        return 0x1907; // GL_RGB;
    } else if (channels == 4) {
        return 0x1908; // GL_RGBA;
    }
    return 0x1907; // GL_RGB;
}

void Image::SetPath(const std::string& path) {
    this->path = path;
}

std::string Image::GetPath() const {
    return path;
}

std::shared_ptr<Image> Image::Duplicate(const std::shared_ptr<Image> image) {
    if (!image) {
        return nullptr;
    }
    auto new_image = Image::Make(image->data, image->width, image->height, image->channels);
    new_image->raw_img_type_ = image->raw_img_type_;
    new_image->path = image->path;
    return new_image;
}
} // namespace px
