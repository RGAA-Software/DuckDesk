//
// Created by RGAA on 2023-12-17.
//

#ifndef TC_APPLICATION_SETTINGS_H
#define TC_APPLICATION_SETTINGS_H

#include <map>
#include <string>
#include "px_steam_manager_new/steam_entities.h"

namespace px
{

    // description
    struct Description {
        std::string author_;
        std::string version_;
    };

    // encoder
    struct Encoder {

        enum EncoderFormat {
            kH264,
            kHEVC,
        };

        enum EncodeResolutionType {
            kOrigin,
            kSpecify,
        };

        EncoderFormat encoder_format_ = EncoderFormat::kH264;
        int fps_ = 60;
        int bitrate_ = 6;
        int encode_width_ = 1280;
        int encode_height_ = 720;
        EncodeResolutionType encode_res_type_ = EncodeResolutionType::kOrigin;
    };

    // capture
    struct Capture {
        enum CaptureAudioType {
            kAudioInner,
            kAudioGlobal,
        };

        enum CaptureVideoType {
            kVideoInner,
            kCaptureScreen,
        };

    public:
        bool IsVideoInnerCapture() const {
            return capture_video_type_ == CaptureVideoType::kVideoInner;
        }

        bool IsAudioInnerCapture() const {
            return capture_audio_type_ == CaptureAudioType::kAudioInner;
        }

    public:
        bool enable_audio_ = true;
        CaptureAudioType capture_audio_type_ = CaptureAudioType::kAudioGlobal;
        bool enable_video_ = true;
        CaptureVideoType capture_video_type_ = CaptureVideoType::kVideoInner;
        std::string capture_audio_device_;
        bool mock_video_ = false;
    };

    // Transmission
    struct Transmission {
        int listening_port_ = 20371;
    };

    // RdApplication
    struct TargetApplication {

        enum InjectMethod {
            kEasyHook,
            kOBS,
        };

        enum EventReplayMode {
            kGlobal,
            kHookInner,
        };

        [[nodiscard]] bool IsGlobalReplayMode() const {
            return event_replay_mode_ == EventReplayMode::kGlobal;
        }

    public:
        std::string game_path_{};
        std::string game_arguments_{};
        // UE boot/view：真游戏(view)进程完整路径，由 service 解析外壳资源下发；
        // 非空时注入目标是该路径的进程而不是 game_path_ 拉起的外壳进程。
        std::string game_view_path_{};
        bool hide_after_started_{};
        bool force_fullscreen_{};
        InjectMethod inject_method_{kEasyHook};
        SteamApp steam_app_;
        bool debug_enabled_{false};
        EventReplayMode event_replay_mode_;

    public:
        [[nodiscard]] bool IsSteamUrl() const {
            return game_path_.find("steam://") != std::string::npos;
        }
    };

    // app mode
    enum class AppMode {
        kDesktop,
        kInnerCapture,
    };

    // High-level launch mode from settings.toml [application].mode
    enum class ApplicationMode {
        kDesktop,
        kGameHook,
    };

    class RdSettings {
    public:

        static RdSettings* Instance() {
            static RdSettings inst;
            return &inst;
        }

        bool LoadSettings(const std::string& path);
        std::string Dump();
        void LoadSettingsFromDatabase();
        bool EnableFullColorMode();
        void SetFullColorMode(bool enable);
        bool IsGameHookMode() const {
            return application_mode_ == ApplicationMode::kGameHook;
        }
        // Apply toml application.mode → capture_video_type_ / app_mode_.
        // Call after CLI UpdateSettings so mode stays toml-driven.
        void ApplyApplicationMode();
    public:
        Description desc_;
        Encoder encoder_{};
        Capture capture_{};
        Transmission transmission_{};
        TargetApplication app_;
        ApplicationMode application_mode_ = ApplicationMode::kDesktop;

        bool block_debug_ = false;
        std::string panel_server_host_ = "127.0.0.1";
        int panel_server_port_ = 0;
        std::string service_server_host_ = "127.0.0.1";
        int service_server_port_ = 20375;
        std::string device_id_;
        std::string device_random_pwd_;
        std::string device_safety_pwd_;
        std::string relay_host_;
        std::string relay_port_;
        // can be operated
        bool can_be_operated_ = true;
        // relay enabled
        bool relay_enabled_ = true;
        // language
        int language_ = 1;
        // file transfer enabled
        bool file_transfer_enabled_ = true;
        // audio enabled
        bool audio_enabled_ = true;
        // app mode
        AppMode app_mode_ = AppMode::kDesktop;
        // appkey
        std::string appkey_;
        // ethernet
        uint64_t max_transmit_speed_ = 0;
        uint64_t max_receive_speed_ = 0;
        int role_ = 1;

    private:
        const std::string kFullColorModeKey = "enable_full_color_mode";
        // 是否启用全彩模式: 如果启用全彩模式, 则编码输出的帧可以解码为yuv444, 否则为yuv420
        bool enable_full_color_mode_ = false;
    };

}

#endif //TC_APPLICATION_SETTINGS_H
