#pragma once

#include <memory>

#include "architecture/sinks/live_pusher_sink.h"

namespace px::render {

std::shared_ptr<LivePushProcessor> MakeFfmpegLivePushProcessor(
    const LivePusherOptions& options,
    const LivePusherSink::KeyframeRequester& request_keyframe);

}  // namespace px::render
