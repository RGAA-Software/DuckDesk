#pragma once

#include "rdp_frame.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace px::rdp {

[[nodiscard]] bool InitializeRdpRuntime();

class SessionSecret final {
  public:
    explicit SessionSecret(std::span<const char> bytes);
    ~SessionSecret();
    SessionSecret(const SessionSecret&) = delete;
    SessionSecret& operator=(const SessionSecret&) = delete;
    [[nodiscard]] std::span<const char> Bytes() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept;

  private:
    std::vector<char> bytes_{};
};

struct SessionConfiguration final {
    std::uint16_t loopbackPort{};
    std::string account{};
    std::string domain{};
    std::string proxyCertificateSha256{};
    std::shared_ptr<const SessionSecret> password{};
    Size desktop{1280, 720};
    bool audio{true};
    bool clipboard{true};
    [[nodiscard]] bool IsValid() const noexcept;
};

enum class SessionPhase : std::uint8_t { Connecting, Connected, Disconnected, Failed };

struct SessionCallbacks final {
    std::function<void(std::shared_ptr<const DesktopFrame>)> frame{};
    std::function<void(SessionPhase, std::string)> phase{};
    std::function<void(std::string)> clipboard{};
};

class RdpSession final {
    struct State;
    struct ConstructionKey final {};

  public:
    [[nodiscard]] static std::shared_ptr<RdpSession> Create(SessionConfiguration configuration, SessionCallbacks callbacks);
    RdpSession(ConstructionKey, std::shared_ptr<State> state);
    ~RdpSession();
    void Stop();
    void Mouse(std::uint16_t flags, int x, int y, bool extended);
    void Key(std::uint32_t scancode, bool down);
    void Unicode(std::uint16_t codepoint, bool down);
    void Synchronize(std::uint16_t toggles);
    void Pause();
    void Resize(Size size);
    void ConsumeFrame(std::uint64_t frameId);
    void Refresh();
    void PublishClipboard(std::string text);

  private:
    std::shared_ptr<State> state_{};
    std::jthread thread_{};
};

} // namespace px::rdp
