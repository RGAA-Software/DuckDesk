#include <iostream>

#include "rd_app.h"
#include "settings/rd_settings.h"
#include "rd_context.h"
#include "tc_common_new/log.h"
#include "tc_common_new/dump_helper.h"
#include "tc_common_new/base64.h"
#include "tc_common_new/folder_util.h"
#include "tc_common_new/hardware.h"
#include "tc_common_new/process_util.h"
#include "gflags/gflags.h"
#include "version_config.h"

#include <Windows.h>
#include <filesystem>

using namespace tc;

DEFINE_int32(steam_app_id, 0, "steam app id");
DEFINE_bool(logfile, true, "log to file");

// encoder
DEFINE_string(encoder_select_type, "auto", "auto/specify");
DEFINE_string(encoder_name, "nvenc", "nvenc/amf/ffmpeg");
DEFINE_string(encoder_format, "h264", "h264/h265");
DEFINE_int32(encoder_bitrate, 20, "encoder bitrate");
DEFINE_int32(encoder_fps, 60, "encoder fps");
DEFINE_string(encoder_resolution_type, "origin", "origin/specify");
DEFINE_int32(encoder_width, 1280, "");
DEFINE_int32(encoder_height, 720, "");

// capture
DEFINE_bool(capture_audio, true, "");
DEFINE_string(capture_audio_type, "global", "inner/global");
DEFINE_bool(capture_video, true, "");
DEFINE_string(capture_video_type, "inner", "inner/global");

// network
DEFINE_bool(webrtc_enabled, true, "");
DEFINE_bool(websocket_enabled, true, "");
DEFINE_int32(network_listen_port, 20371, "");
DEFINE_bool(udp_kcp_enabled, true, "");

DEFINE_string(sig_server_address, "", "");
DEFINE_string(sig_server_port, "", "");
DEFINE_string(coturn_server_address, "", "");
DEFINE_string(coturn_server_port, "", "");

DEFINE_string(capture_audio_device, "", "capture audio device");

// application
// --app_game_path is Base64(UTF-8 path) to avoid Windows argv code-page issues (spaces/Chinese).
DEFINE_string(app_game_path, "", "Base64-encoded UTF-8 game path");
// --app_game_view_path: Base64(UTF-8)，UE 外壳场景的真游戏进程完整路径（service 解析下发）
DEFINE_string(app_game_view_path, "", "Base64-encoded UTF-8 UE view (real game) exe path");
DEFINE_string(app_game_args, "", "");

DEFINE_bool(debug_block, false, "block the render process");
DEFINE_bool(mock_video, false, "use mocking video plugin");

DEFINE_string(device_id, "", "device id");
DEFINE_string(device_random_pwd, "", "device random pwd");
DEFINE_string(device_safety_pwd, "", "device safety pwd");

DEFINE_string(relay_server_host, "", "relay host");
DEFINE_string(relay_server_port, "", "relay port");

DEFINE_string(panel_server_host, "127.0.0.1", "");
DEFINE_int32(panel_server_port, 0, "");
DEFINE_string(service_server_host, "127.0.0.1", "");
DEFINE_int32(service_server_port, 20375, "");
// can be operated by mouse / keyboard
DEFINE_bool(can_be_operated, true, "");
// file transfer enabled
DEFINE_bool(file_transfer_enabled, true, "");
// audio enabled
DEFINE_bool(audio_enabled, true, "");

// relay enabled
DEFINE_bool(relay_enabled, true, "");

DEFINE_int32(language, 0, "");

DEFINE_string(app_mode, "", "desktop | game-hook | inner_capture; empty => settings.toml application.mode");
// appkey
DEFINE_string(appkey, "", "appkey");

void UpdateSettings(RdSettings* settings) {
    if (FLAGS_steam_app_id > 0) {
        settings->app_.steam_app_.app_id_ = FLAGS_steam_app_id;
        settings->app_.steam_app_.steam_url_ = std::format("steam://rungameid/{}", FLAGS_steam_app_id);
    }

    if (FLAGS_encoder_format == "h264") {
        settings->encoder_.encoder_format_ = Encoder::EncoderFormat::kH264;
    }
    else {
        settings->encoder_.encoder_format_ = Encoder::EncoderFormat::kHEVC;
    }

    settings->encoder_.bitrate_ = FLAGS_encoder_bitrate;
    settings->encoder_.fps_ = FLAGS_encoder_fps;

    if (FLAGS_encoder_resolution_type == "origin") {
        settings->encoder_.encode_res_type_ = Encoder::EncodeResolutionType::kOrigin;
    }
    else {
        settings->encoder_.encode_res_type_ = Encoder::EncodeResolutionType::kSpecify;
    }
    settings->encoder_.encode_width_ = FLAGS_encoder_width;
    settings->encoder_.encode_height_ = FLAGS_encoder_height;

    // capture
    settings->capture_.enable_audio_ = FLAGS_capture_audio;
    if (FLAGS_capture_audio_type == "global") {
        settings->capture_.capture_audio_type_ = Capture::CaptureAudioType::kAudioGlobal;
    }
    else {
        settings->capture_.capture_audio_type_ = Capture::CaptureAudioType::kAudioInner;
    }

    settings->capture_.enable_video_ = FLAGS_capture_video;
    if (FLAGS_capture_video_type == "global") {
        settings->capture_.capture_video_type_ = Capture::CaptureVideoType::kCaptureScreen;
    }
    else {
        settings->capture_.capture_video_type_ = Capture::CaptureVideoType::kVideoInner;
    }
    // Ignored: audio capture plugin always uses the OS default playback device.
    settings->capture_.capture_audio_device_.clear();
    (void)FLAGS_capture_audio_device;
    settings->transmission_.listening_port_ = FLAGS_network_listen_port;

    // app: path arrives as Base64(UTF-8); decode with existing Base64 helper (no ACP convert).
    if (!FLAGS_app_game_path.empty()) {
        settings->app_.game_path_ = Base64::Base64Decode(FLAGS_app_game_path);
    }
    if (!FLAGS_app_game_view_path.empty()) {
        settings->app_.game_view_path_ = Base64::Base64Decode(FLAGS_app_game_view_path);
    }
    if (!FLAGS_app_game_args.empty()) {
        settings->app_.game_arguments_ = FLAGS_app_game_args;
    }

    settings->block_debug_ = FLAGS_debug_block;
    settings->capture_.mock_video_ = FLAGS_mock_video;

    settings->device_id_ = FLAGS_device_id;
    settings->device_random_pwd_ = FLAGS_device_random_pwd;
    settings->device_safety_pwd_ = FLAGS_device_safety_pwd;

    settings->relay_host_ = FLAGS_relay_server_host;
    settings->relay_port_ = FLAGS_relay_server_port;

    settings->panel_server_host_ = FLAGS_panel_server_host;
    settings->panel_server_port_ = FLAGS_panel_server_port;
    settings->service_server_host_ = FLAGS_service_server_host;
    settings->service_server_port_ = FLAGS_service_server_port;

    // can be operated
    settings->can_be_operated_ = FLAGS_can_be_operated;
    // file transfer enabled
    settings->file_transfer_enabled_ = FLAGS_file_transfer_enabled;
    // audio enabled
    settings->audio_enabled_ = FLAGS_audio_enabled;
    // relay enabled
    settings->relay_enabled_ = FLAGS_relay_enabled;
    // language
    settings->language_ = FLAGS_language;
    // app mode: explicit CLI overrides settings.toml application.mode
    if (FLAGS_app_mode == "desktop") {
        settings->application_mode_ = ApplicationMode::kDesktop;
    }
    else if (FLAGS_app_mode == "game-hook" || FLAGS_app_mode == "inner_capture") {
        settings->application_mode_ = ApplicationMode::kGameHook;
    }

    // appkey
    settings->appkey_ = FLAGS_appkey;
}

void PrintInputArgs() {
    auto settings = RdSettings::Instance();
    LOGI("--------------In args begin--------------");
    LOGI("steam_app_id: {}", FLAGS_steam_app_id);
    LOGI("logfile: {}", FLAGS_logfile);
    LOGI("encoder_select_type: {}", FLAGS_encoder_select_type);
    LOGI("encoder_name: {}", FLAGS_encoder_name);
    LOGI("encoder_format: {}", FLAGS_encoder_format);
    LOGI("encoder_bitrate: {}", FLAGS_encoder_bitrate);
    LOGI("encoder_fps: {}", FLAGS_encoder_fps);
    LOGI("encoder_resolution_type: {}", FLAGS_encoder_resolution_type);
    LOGI("encoder_width: {}", FLAGS_encoder_width);
    LOGI("encoder_height: {}", FLAGS_encoder_height);
    LOGI("capture_audio: {}", FLAGS_capture_audio);
    LOGI("capture_audio_type: {}", FLAGS_capture_audio_type);
    LOGI("capture_video: {}", FLAGS_capture_video);
    LOGI("capture_video_type: {}", FLAGS_capture_video_type);
    LOGI("websocket enabled: {}", FLAGS_websocket_enabled);
    LOGI("webrtc enabled: {}", FLAGS_webrtc_enabled);
    LOGI("network_listen_port: {}", FLAGS_network_listen_port);
    LOGI("capture audio device: <os-default>");
    LOGI("app_game_path(b64): {}", FLAGS_app_game_path);
    LOGI("app_game_path: {}", settings->app_.game_path_);
    LOGI("app_game_args: {}", FLAGS_app_game_args);
    LOGI("block debug: {}", FLAGS_debug_block);
    LOGI("mock video: {}", FLAGS_mock_video);
    LOGI("sig server address: {}", FLAGS_sig_server_address);
    LOGI("sig server port: {}", FLAGS_sig_server_port);
    LOGI("coturn server address: {}", FLAGS_coturn_server_address);
    LOGI("coturn server port: {}", FLAGS_coturn_server_port);
    LOGI("device id: {}", FLAGS_device_id);
    LOGI("device random pwd: {}", FLAGS_device_random_pwd);
    LOGI("panel server host: {}", FLAGS_panel_server_host);
    LOGI("panel server port: {}", FLAGS_panel_server_port);
    LOGI("service server host: {}", FLAGS_service_server_host);
    LOGI("service server port: {}", FLAGS_service_server_port);
    LOGI("relay host: {}", FLAGS_relay_server_host);
    LOGI("relay port: {}", FLAGS_relay_server_port);
    LOGI("can be operated: {}", FLAGS_can_be_operated);
    LOGI("file transfer enabled: {}", settings->file_transfer_enabled_);
    LOGI("audio enabled: {}", settings->audio_enabled_);
    LOGI("relay enabled: {}", FLAGS_relay_enabled);
    LOGI("language: {}", FLAGS_language);
    LOGI("app mode: {} => {}", FLAGS_app_mode, (int)settings->app_mode_);
    LOGI("event replay mode: {} (0=global,1=inner)", (int)settings->app_.event_replay_mode_);
    LOGI("appkey : {}", FLAGS_appkey);
    LOGI("--------------In args end----------------");
}

HANDLE g_instance_mutex = NULL;
bool CanWeRun(const std::wstring& lock_path) {
    g_instance_mutex = CreateMutexW(NULL, TRUE, lock_path.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_instance_mutex) {
            CloseHandle(g_instance_mutex);
            g_instance_mutex = NULL;
        }
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    // hook 模式下 render 需要按游戏窗口真实物理像素换算鼠标坐标；
    // 不设 DPI aware 时 GetClientRect/ClientToScreen 会被系统虚拟化（如 4K@150% 下只有 2560x1440），
    // 导致游戏内光标位置整体偏向左上角
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // dump
    //CaptureDump();
    // Breakpad
    auto bc = BreakpadContext {
        .version_ = PROJECT_VERSION,
        .app_name_ = "GammaRayRender",
    };
    CaptureDumpByBreakpad(&bc);

    // run in high level
    tc::ProcessUtil::SetProcessInHighLevel();
    // 混合架构 CPU(8P+8E):钉到大核,避免采集/编码线程被调度到小核
    tc::ProcessUtil::PinToPerformanceCores();

    // 1. settings.toml defaults (application.mode / game-path / capture-method)
    // 2. CLI overrides (panel: --app_mode=desktop; game-hook script: --app_mode=game-hook)
    // 3. ApplyApplicationMode syncs capture path + whether to launch game-path
    auto settings = RdSettings::Instance();
    settings->LoadSettings("settings.toml");
    UpdateSettings(settings);
    settings->ApplyApplicationMode();
    settings->LoadSettingsFromDatabase();

    // Log
    auto log_file_path = std::format(L"{}/gr_logs/godesk_render_{}.log",
         FolderUtil::GetProgramDataPath(), settings->transmission_.listening_port_);
    Logger::InitLog(log_file_path, FLAGS_logfile);

    PrintInputArgs();

    auto settings_str = settings->Dump();
    LOGI("\n" + settings_str);

    //settings->block_debug_ = true;
    if (settings->block_debug_) {
        MessageBoxA(0, 0, 0, 0);
    }

    auto lock_name = std::format(L"godesk_render_lock_{}", settings->transmission_.listening_port_);
    wchar_t temp_path[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_path);
    auto lock_path = (std::filesystem::path(temp_path) / lock_name).wstring();
    auto can_we_run = CanWeRun(lock_path);
    if (!can_we_run) {
        LOGE("We can't run because of already running instance!");
        auto reason = std::format(L"Already locked at: {}", lock_path);
        MessageBoxW(NULL, reason.c_str(), L"Start render failed!", MB_OK | MB_ICONERROR);
        return -1;
    }

    // start application
    tc::AppParams params = {};
    auto app = tc::RdApplication::Make(params);
    app->Init(argc, argv);
    app->CaptureControlC();

    // hardware
    auto hardware = Hardware::Instance();
    hardware->Detect(false, true, false);
    hardware->Dump();

    return app->Run();
}
