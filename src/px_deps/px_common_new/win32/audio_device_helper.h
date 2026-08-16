//
// Created by RGAA on 6/08/2024.
//

#ifndef PX_AUDIO_DEVICE_HELPER_H
#define PX_AUDIO_DEVICE_HELPER_H

#include <string>
#include <vector>

namespace px
{

    class AudioDevice {
    public:
        bool output_ = true;
        std::string name_;
        std::string id_;
        bool default_device_ = false;
    };

    class AudioDeviceHelper {
    public:

        static std::vector<AudioDevice> DetectAudioDevices();

    };

}

#endif //PX_AUDIO_DEVICE_HELPER_H
