#pragma once

#include <Windows.h>
#include <memory>
#include <mutex>
#include "px_capture/capture_text_input.h"

namespace px {

// The owner is a window-property registration, not the HWND's lifetime. Windows removes
// properties on window destruction, which makes a reused numeric HWND fail validation.
struct TextWindowRegistrationDeleter final {
    void operator()(HWND window) const noexcept; // NOLINT(gammaray-raw-pointer-boundary) Win32 property registration cleanup boundary.
};
using TextWindowRegistration = std::unique_ptr<HWND__, TextWindowRegistrationDeleter>;

class HookGameTextInput final {
  public:
    [[nodiscard]] CaptureTextReply Execute(const CaptureTextCommand& command);
    void Reset();

  private:
    std::mutex mutex_{};
    TextWindowRegistration target_{};
    std::uint64_t generation_{};
    std::optional<bool> previous_imm_association_{};
    CaptureTextEditability imm_hint_{CaptureTextEditability::kUnknown};
};

} // namespace px
