#pragma once
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace px {
enum RawImageFormat {
    kRawImageRGB,
    kRawImageRGBA,
    kRawImageNV12,
    kRawImageI420,
    kRawImageI444,
    kRawImageD3D11Texture,
    kRawImageVulkanAVFrame,
    kRawImagePresented,
};
struct ImagePlane final {
    std::size_t offset{};
    std::size_t size{};
    int stride{};
    int rows{};
};
class PlatformImage {
  public:
    virtual ~PlatformImage() = default;
    [[nodiscard]] virtual std::shared_ptr<PlatformImage> Clone() const = 0;
};
class RawImage final {
  public:
    [[nodiscard]] static std::shared_ptr<RawImage> Make(RawImageFormat format, int width, int height, std::span<const char> source = {});
    [[nodiscard]] static std::shared_ptr<RawImage> MakePresented(int width, int height);
    [[nodiscard]] static std::shared_ptr<RawImage> MakePlatform(RawImageFormat format, int width, int height, std::shared_ptr<PlatformImage> storage);
    RawImage() = default;
    [[nodiscard]] std::span<const char> Bytes() const noexcept {
        return buffer_;
    }
    [[nodiscard]] std::span<char> MutableBytes() noexcept {
        return buffer_;
    }
    [[nodiscard]] std::span<const char> Plane(std::size_t index) const noexcept;
    [[nodiscard]] std::span<char> MutablePlane(std::size_t index) noexcept;
    [[nodiscard]] std::optional<ImagePlane> Layout(std::size_t index) const noexcept;
    [[nodiscard]] int Size() const noexcept {
        return static_cast<int>(buffer_.size());
    }
    [[nodiscard]] RawImageFormat Format() const noexcept {
        return img_format;
    }
    [[nodiscard]] std::shared_ptr<const PlatformImage> Platform() const noexcept {
        return platform_;
    }
    [[nodiscard]] std::shared_ptr<RawImage> Clone() const;
    void CopyTo(const std::shared_ptr<RawImage>& target) const;
    void SaveYUV444ToFile(const std::string& filename) const;
    void AppendYUV444ToFile(const std::string& filename) const;
    int img_width{};
    int img_height{};
    int img_ch{-1};
    RawImageFormat img_format{kRawImageRGB};
    bool full_color_{};

  private:
    std::vector<char> buffer_{};
    std::array<ImagePlane, 3> planes_{};
    std::size_t plane_count_{};
    std::shared_ptr<PlatformImage> platform_{};
};
using RawImagePtr = std::shared_ptr<RawImage>;
} // namespace px
