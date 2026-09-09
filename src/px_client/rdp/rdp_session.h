#pragma once

#include "rdp_frame.h"
#include "rdp_clipboard_data.h"
#include <QImage>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace px::rdp {

// Called by the composition root before network/protocol workers initialize SSL.
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
    std::uint16_t loopback_port{0};
    std::string account{};
    std::string domain{};
    std::string proxy_certificate_sha256{};
    std::shared_ptr<const SessionSecret> password{};
    QSize desktop{1280, 720};
    bool audio{true};
    bool clipboard{true};
    [[nodiscard]] bool IsValid() const noexcept;
};

enum class SessionPhase { kConnecting, kConnected, kDisconnected, kFailed };
enum class PointerOperation { kCreate, kActivate, kRemove, kDefault, kNull };
struct PointerUpdate final {
    PointerOperation operation{PointerOperation::kDefault};
    std::uint64_t id{0};
    QImage image{};
    QPoint hotspot{};
};
struct SessionCallbacks final {
    // Invoked on the protocol worker. UI composition must queue owned values
    // using weak controller references and reject callbacks from old generations.
    std::function<void(std::shared_ptr<const DesktopFrame>)> frame{};
    std::function<void(PointerUpdate)> pointer{};
    std::function<void(SessionPhase, std::string)> phase{};
    std::function<void(std::shared_ptr<const ClipboardData>)> clipboard{};
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
    void Resize(QSize size);
    void ConsumeFrame(std::uint64_t frame_id);
    void Refresh();
    void PublishClipboard(std::shared_ptr<const ClipboardData> data);

  private:
    std::shared_ptr<State> state_{};
    std::jthread thread_{};
};

} // namespace px::rdp
