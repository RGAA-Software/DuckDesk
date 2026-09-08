#pragma once

#include <memory>
#include <string>

extern "C" {
#include <libavutil/buffer.h>
}

namespace px {
class D3D11DeviceWrapper;

// Prepared by the composition root before SDK Init; never mutated after publication.
struct WindowsVideoResources final {
    std::shared_ptr<D3D11DeviceWrapper> d3d11{};
    // This owner also retains the renderer/queue lifetime behind the device.
    std::shared_ptr<AVBufferRef> vulkan_device{};
    bool use_vulkan{};
    std::string decoder_preference{"Auto"};
};
} // namespace px
