#ifndef IMAGE_H
#define IMAGE_H
#include <string>
#include "data.h"
namespace tc
{

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
        static std::shared_ptr<Image> Make(const char* data, int width, int height, int channels);
        static std::shared_ptr<Image> Make(const DataPtr& data, int width, int height, int channels);
        static std::shared_ptr<Image> Make(const DataPtr&, int width, int height);
        static std::shared_ptr<Image> Make(const DataPtr&, int width, int height, const RawImageType& rt);
#ifdef WIN32
        // 图片是jpg png等有压缩格式的
        static std::shared_ptr<Image> MakeByCompressedImage(const DataPtr& data);
#endif

        Image() = delete;
        ~Image();
        Image(const char* data, int width, int height, int channels);
        Image(const DataPtr& img_data, int width, int height, int channels);
        Image(const DataPtr& img_data);
        std::shared_ptr<Image> Duplicate(const std::shared_ptr<Image> image);

        int GetWidth();
        int GetHeight();
        int GetChannels();
        DataPtr GetData();
        int GetInternalFormat();
        std::string GetPath();

        void SetPath(const std::string& path);

    public:
        int width;
        int height;
        int channels;
        DataPtr data;
        std::string path;
        RawImageType raw_img_type_{};
    };

    typedef std::shared_ptr<Image> ImagePtr;

}

#endif // IMAGE_H
