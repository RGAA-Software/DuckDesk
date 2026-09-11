#pragma once

#include <expected>
#include <memory>
#include <string>

namespace px::desktop {

class WindowHost;

class D3d11Renderer final {
  public:
    static std::expected<D3d11Renderer, std::string> Create(const WindowHost& window);

    D3d11Renderer(D3d11Renderer&&) noexcept;
    D3d11Renderer& operator=(D3d11Renderer&&) noexcept;
    ~D3d11Renderer();

    D3d11Renderer(const D3d11Renderer&) = delete;
    D3d11Renderer& operator=(const D3d11Renderer&) = delete;

    bool InitializeImGuiBackend();
    void ShutdownImGuiBackend();
    void BeginImGuiFrame() const;
    bool Resize(int width, int height);
    void Render() const;

  private:
    struct Impl;

    explicit D3d11Renderer(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_{};
};

} // namespace px::desktop
