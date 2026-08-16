//
// Created by RGAA on 25/11/2024.
//

#ifndef PX_FRAME_PROCESSOR_H
#define PX_FRAME_PROCESSOR_H

#include "px_plugin_interface.h"
#include <optional>
#include <d3d11.h>
#include <wrl/client.h>

using namespace Microsoft::WRL;

namespace px
{

    enum class FrameProcessorType {
        kPrev, // before encode
        kPost, // maybe for debugging
    };

    // Resize Info
    class PxFrameResizeInfo {
    public:
        std::string mon_name_;
        int resize_width_ = 0;
        int resize_height_ = 0;
    };

    class PxFrameProcessorPlugin : public PxPluginInterface {
    public:
        PxFrameProcessorPlugin();

        virtual ComPtr<ID3D11Texture2D> Process(const ComPtr<ID3D11Texture2D>& input, uint64_t adapter_uid, const std::string& monitor_name, int target_width, int target_height) = 0;

        virtual std::optional<PxFrameResizeInfo> GetFrameResizeInfo(const std::string& mon_name) {
            return std::nullopt;
        }

    public:
        int priority_ = 0;
        FrameProcessorType processor_type_;
    };

}

#endif //PX_FRAME_PROCESSOR_H
