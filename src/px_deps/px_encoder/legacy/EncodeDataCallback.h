#pragma once

#include "px_common/data.h"
#include <functional>

typedef std::function<void(std::shared_ptr<px::Data> data, uint64_t frame_idx, bool key)> EncodeDataCallback;
typedef std::function<void(bool success)> InitEncoderCallback;
