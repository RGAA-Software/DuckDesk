#pragma once

#include "client_controller_position.h"
#include "client_recording_result.h"
#include "px_ui/components/feedback.h"

namespace px::client::imgui {

[[nodiscard]] px::ui::ToastMessage RecordingToast(const ClientRecordingResult& result, bool english);
[[nodiscard]] float RecordingPulseOpacity(double elapsedSeconds) noexcept;
// Returns the vertical space reserved above top-right toasts. Foreground text never captures remote input.
[[nodiscard]] float DrawRecordingIndicator(bool active, const ControllerArea& contentArea, double elapsedSeconds, bool english);

}  // namespace px::client::imgui
