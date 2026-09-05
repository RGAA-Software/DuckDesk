#ifndef IMAGE_H
#define IMAGE_H
#include <cstdint>
#include <string>
#include "data.h"
namespace px {

enum class RawImageType {
    kRGB,
    kBGR,
    kRGBA,
    kBGRA,
    kI420,
    kI444,
};

class Image {
  public:
    static std::shared_ptr<Image> Make(const DataPtr& data, int width, int height, int channels);
    static std::shared_ptr<Image> Make(const DataPtr&, int width, int height);
    static std::shared_ptr<Image> Make(const DataPtr&, int width, int height, const RawImageType& rt);
#ifdef WIN32
    // 图片是jpg png等有压缩格式的
    static std::shared_ptr<Image> MakeByCompressedImage(const DataPtr& data);
#endif

    Image() = delete;
    ~Image();
    Image(const DataPtr& img_data, int width, int height, int channels);
    Image(const DataPtr& img_data);
    std::shared_ptr<Image> Duplicate(const std::shared_ptr<Image> image);

    [[nodiscard]] int GetWidth() const;
    [[nodiscard]] int GetHeight() const;
    [[nodiscard]] int GetChannels() const;
    [[nodiscard]] DataPtr GetData() const;
    [[nodiscard]] int GetInternalFormat() const;
    [[nodiscard]] std::string GetPath() const;

    void SetPath(const std::string& path);

  public:
    int width = 0;
    int height = 0;
    int channels = 0;
    DataPtr data{};
    std::string path{};
    RawImageType raw_img_type_{};
};

typedef std::shared_ptr<Image> ImagePtr;

} // namespace px

#endif // IMAGE_H
