//
// Created by hy on 2024/3/27.
//

#include "native_msg_maker.h"
#include <nlohmann/json.hpp>
#include "px_common/audio_filter.h"

using namespace nlohmann;

namespace px
{

    std::string NativeMsgMaker::MakeFrameInfoMessage(int width, int height, int format,
                                                     const std::string& mon_name, int mon_left,
                                                     int mon_top, int mon_right, int mon_bottom) {
        json msg;
        msg["type"] = "frame";
        msg["width"] = width;
        msg["height"] = height;
        msg["format"] = format;
        msg["mon_name"] = mon_name;
        msg["mon_left"] = mon_left;
        msg["mon_top"] = mon_top;
        msg["mon_right"] = mon_right;
        msg["mon_bottom"] = mon_bottom;
        return msg.dump();
    }

    std::string NativeMsgMaker::MakeSpectrumMessage(const px::RendererAudioSpectrum& spectrum) {
        json msg;
        msg["type"] = "spectrum";
        auto left_spectrum = json::array();
        auto right_spectrum = json::array();

        std::vector<double> left_spectrum_value;
        for (int i = 0; i < spectrum.left_spectrum_size(); i++) {
            left_spectrum_value.push_back(spectrum.left_spectrum(i));
        }
        std::vector<double> right_spectrum_value;
        for (int i = 0; i < spectrum.right_spectrum_size(); i++) {
            right_spectrum_value.push_back(spectrum.right_spectrum(i));
        }

        MonsterCatFilter::FilterBars(left_spectrum_value);
        MonsterCatFilter::FilterBars(right_spectrum_value);
        for (auto& value : left_spectrum_value) {
            left_spectrum.push_back(value);
        }
        for (auto& value : right_spectrum_value) {
            right_spectrum.push_back(value);
        }

        msg["left_spectrum"] = left_spectrum;
        msg["right_spectrum"] = right_spectrum;
        return msg.dump();
    }

    std::string NativeMsgMaker::MakeServerConfigurationMessage(const px::ServerConfiguration& config) {
        json msg;
        msg["type"] = "server_configuration";
        msg["capturing_monitor_name"] = config.capturing_monitor_name();
        msg["fps"] = config.fps();
        int mon_size = config.monitors_info_size();
        auto mon_array = json::array();
        for (int i = 0; i < mon_size; i++) {
            auto& this_mon = config.monitors_info(i);
            // the monitor
            json mon_item;
            mon_item["name"] = this_mon.name();
            // the supported resolutions
            auto res_array = json::array();
            for (int j = 0; j < this_mon.resolutions_size(); j++) {
                res_array.push_back(std::format("{}x{}", this_mon.resolutions(j).width(), this_mon.resolutions(j).height()));
            }
            mon_item["resolutions"] = res_array;

            //
            mon_array.push_back(mon_item);
        }
        msg["monitors"] = mon_array;

        return msg.dump();
    }

}