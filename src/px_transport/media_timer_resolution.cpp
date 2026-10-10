#include "media_timer_resolution.h"

#ifdef _WIN32
#include <Windows.h>
#include <timeapi.h>
#endif

namespace px::transport {

MediaTimerResolution::MediaTimerResolution() {
#ifdef _WIN32
    active_ = timeBeginPeriod(1) == TIMERR_NOERROR;
#endif
}

MediaTimerResolution::~MediaTimerResolution() {
#ifdef _WIN32
    if (active_) timeEndPeriod(1);
#endif
}

}  // namespace px::transport
