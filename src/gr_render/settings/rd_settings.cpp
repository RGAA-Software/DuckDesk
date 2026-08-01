//
// Created by RGAA on 2023-12-17.
//

#include "rd_settings.h"

#include <sstream>

#include <toml++/toml.hpp>
#include "tc_common_new/string_util.h"
#include "tc_common_new/log.h"
#include "tc_common_new/shared_preference.h"

namespace tc
{

    bool RdSettings::LoadSettings(const std::string& path) {
        toml::parse_result result;
        try {
            result = toml::parse_file(path);
        } catch (std::exception& e) {
            return false;
        }

        // description
        desc_.author_ = result["description"]["author"].value_or("");
        desc_.version_ = result["description"]["version"].value_or("0.0.1");

        // NOTE: encoder/capture/transmission are no longer read from this file.
        // Desktop mode: the panel passes them as command line args (see UpdateSettings).
        // Standalone: built-in defaults in rd_settings.h are used.

        // TargetApplication
        app_.game_path_ = result["application"]["game-path"].value_or("");
        app_.game_arguments_ = result["application"]["game-arguments"].value_or("");
        app_.hide_after_started_ = result["application"]["hide-after-started"].value_or(false);
        app_.force_fullscreen_ = result["application"]["force-fullscreen"].value_or(false);
        auto inject_method = result["application"]["capture-method"].value_or("obs");
        app_.inject_method_ = [&]() -> TargetApplication::InjectMethod {
            return std::string(inject_method) == "prepare"
                ? TargetApplication::InjectMethod::kEasyHook : TargetApplication::InjectMethod::kOBS;
        }();
        if (app_.IsSteamUrl()) {
            std::vector<std::string> split_value;
            StringUtil::Split(app_.game_path_, split_value, "/");
            if (!split_value.empty()) {
                auto id = std::atoi(split_value[split_value.size()-1].c_str());
                app_.steam_app_.app_id_ = id;
            }
            app_.steam_app_.steam_url_ = app_.game_path_;
        }
        app_.debug_enabled_ = result["application"]["debug-enabled"].value_or(false);
        app_.event_replay_mode_ = std::string("global") == result["application"]["event-replay-mode"].value_or("global")
                                  ? TargetApplication::EventReplayMode::kGlobal : TargetApplication::EventReplayMode::kHookInner;
        return true;
    }

    std::string RdSettings::Dump() {
        std::stringstream ss;
        ss << "Description: \n";
        ss << "  - author: " << desc_.author_ << std::endl;
        ss << "  - version: " << desc_.version_ << std::endl;
        ss << "Encoder: \n";
        ss << "  - encoder format: " << encoder_.encoder_format_ << " (0 => H264, 1 => HEVC)" << std::endl;
        ss << "  - bitrate: " << encoder_.bitrate_ << std::endl;
        ss << "  - encode resolution type: " << (int)encoder_.encode_res_type_ << " (0 => origin, 1=> specify) " <<  std::endl;
        ss << "  - encode fps: " << encoder_.fps_ << std::endl;
        ss << "  - encode width: " << encoder_.encode_width_ << ", height: " << encoder_.encode_height_ << std::endl;
        ss << "Capture: \n";
        ss << "  - enable audio: " << capture_.enable_audio_ << std::endl;
        ss << "  - capture audio type: " << capture_.capture_audio_type_ << " (0 => Hook, 1 => Global) " << std::endl;
        ss << "  - enable audio: " << capture_.enable_video_ << std::endl;
        ss << "  - capture video type: " << capture_.capture_video_type_ << " (0 => Hook 1 => Primary Screen) " << std::endl;
        ss << "Transmission: \n";
        ss << "  - listening port: " << transmission_.listening_port_ << std::endl;
        ss << "RdApplication: \n";
        ss << "  - game path: " << app_.game_path_ << std::endl;
        ss << "  - game arguments: " << app_.game_arguments_ << std::endl;
        ss << "  - steam app:" << std::endl;
        ss << "    - app id: " << app_.steam_app_.app_id_ << std::endl;
        ss << "    - steam url: " << app_.steam_app_.steam_url_ << std::endl;
        ss << "  - hide after started: " << app_.hide_after_started_ << std::endl;
        ss << "  - force fullscreen: " << app_.force_fullscreen_ << std::endl;
        ss << "  - event relay mode: " << app_.event_replay_mode_ << std::endl;
        return ss.str();
    }


    void RdSettings::LoadSettingsFromDatabase() {
        auto sp = SharedPreference::Instance();
        enable_full_color_mode_ = sp->GetInt(kFullColorModeKey, 0);
    }

    bool RdSettings::EnableFullColorMode() {
        return enable_full_color_mode_;
    }

    void RdSettings::SetFullColorMode(bool enable) {
        enable_full_color_mode_ = enable;
        auto sp = SharedPreference::Instance();
        sp->PutInt(kFullColorModeKey, enable ? 1 : 0);
    }
}