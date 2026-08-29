#pragma once

#include <memory>

#include "live_pusher_runtime.h"

namespace px {

std::shared_ptr<LivePushProcessor> MakeFfmpegLivePushProcessor(
    const LivePusherRuntime::Config& config,
    const LivePusherRuntime::KeyframeRequester& request_keyframe);

}  // namespace px
