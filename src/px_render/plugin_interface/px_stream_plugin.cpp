//
// Created by RGAA on 19/11/2024.
//

#include "px_stream_plugin.h"

namespace px
{

    PxStreamPlugin::PxStreamPlugin() : PxPluginInterface() {
        plugin_type_ = PxPluginType::kStream;
    }

    PxStreamPlugin::~PxStreamPlugin() {

    }

    void PxStreamPlugin::OnVideoEncoderCreated(const std::string& mon_name, const PxPluginEncodedVideoType& type, int width, int height) {

    }

    void PxStreamPlugin::OnEncodedVideoFrame(const std::string& mon_name,
                                            const PxPluginEncodedVideoType& video_type,
                                            const std::shared_ptr<Data>& data,
                                            uint64_t frame_index,
                                            int frame_width,
                                            int frame_height,
                                            bool key) {

    }

    void PxStreamPlugin::OnRawAudioData(const std::shared_ptr<Data>& data, int samples, int channels, int bits) {

    }

    void PxStreamPlugin::OnSplitRawAudioData(const std::shared_ptr<Data>& left_ch_data,
                                             const std::shared_ptr<Data>& right_ch_data,
                                             int samples, int channels, int bits) {

    }

    void PxStreamPlugin::OnSplitFFTAudioData(const std::vector<double>& left_fft, const std::vector<double>& right_fft) {

    }
}