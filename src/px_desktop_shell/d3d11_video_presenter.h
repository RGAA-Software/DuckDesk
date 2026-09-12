#pragma once

#include <cstdint>
#include <memory>

namespace px {
class D3D11DeviceWrapper;
class RawImage;
} // namespace px

namespace px::desktop {

class D3d11VideoPresenter final {
  public:
    static std::shared_ptr<D3d11VideoPresenter> Create(std::shared_ptr<D3D11DeviceWrapper> device);

    explicit D3d11VideoPresenter(std::shared_ptr<D3D11DeviceWrapper> device);
    ~D3d11VideoPresenter();

    D3d11VideoPresenter(const D3d11VideoPresenter&) = delete;
    D3d11VideoPresenter& operator=(const D3d11VideoPresenter&) = delete;

    bool Initialize();
    bool Present(const std::shared_ptr<RawImage>& image);
    [[nodiscard]] std::uint64_t TextureId() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace px::desktop
