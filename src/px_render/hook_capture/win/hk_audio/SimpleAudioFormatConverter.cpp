#include "SimpleAudioFormatConverter.h"

#include <vector>

#include "px_common_new/data.h"

namespace px {

std::shared_ptr<Data> SimpleAudioFormatConverter::CvtF32ToS16(std::shared_ptr<Data> origin) {
    if (!origin) {
        return nullptr;
    }
    return CvtF32ToS16(origin->MutableBytes().data(), static_cast<int>(origin->Size()));
}

std::shared_ptr<Data> SimpleAudioFormatConverter::CvtF32ToS16(char* origin, int origin_size) {
    if (!origin || origin_size <= 0) {
        return nullptr;
    }
    const int sample_count = origin_size / static_cast<int>(sizeof(float));
    auto* f32 = reinterpret_cast<float*>(origin);
    std::vector<int16_t> result(static_cast<size_t>(sample_count));
    for (int i = 0; i < sample_count; i++) {
        float v = f32[i];
        if (v > 1.0f) {
            v = 1.0f;
        } else if (v < -1.0f) {
            v = -1.0f;
        }
        result[static_cast<size_t>(i)] = static_cast<int16_t>(v * 32767.0f);
    }
    return Data::Copy(std::span<const char>{reinterpret_cast<const char*>(result.data()), result.size() * sizeof(std::int16_t)});
}

}  // namespace px
