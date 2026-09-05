#pragma once

#include <functional>

namespace px {

enum class MonitorCaptureError {
    kCantCapture,
    kTimeoutSoManyTimes,
};

using CaptureErrorCallback = std::function<void(MonitorCaptureError error)>;

} // namespace px
