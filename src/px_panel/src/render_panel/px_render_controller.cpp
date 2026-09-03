//
// Created by RGAA on 2024-03-30.
//

#include "px_render_controller.h"
#include "px_settings.h"
#include "px_application.h"
#include "px_service_message.pb.h"
#include "px_common_new/log.h"
#include "px_common_new/base64.h"
#include "px_common_new/string_util.h"
#include "translator/px_translator.h"
#include <QCoreApplication>

namespace px
{

    PxRenderController::PxRenderController(const std::shared_ptr<PxApplication>& app) {
        app_ = app;
        context_ = app_->GetContext();
    }

    PxRenderController::~PxRenderController() {
        Exit();
    }

    bool PxRenderController::StartServer() {
        auto args = this->GetArgs();
        LOGI("StartServer Params:");
        QStringList arg_list;
        for (auto& arg : args) {
            arg_list << arg.c_str();
            LOGI("{}", arg);
        }

        //
        px::ServiceMessage srv_msg;
        srv_msg.set_type(ServiceMessageType::kSrvStartServer);
        auto sub = srv_msg.mutable_start_server();
        sub->set_work_dir(GetWorkDir().toStdString());
        sub->set_app_path(GetAppPath().toStdString());
        for (auto& arg : args) {
            sub->add_args(arg);
        }
        app_->PostMessage2Service(srv_msg.SerializeAsString());
        return true;
    }

    bool PxRenderController::StopServer() {
        px::ServiceMessage srv_msg;
        srv_msg.set_type(ServiceMessageType::kSrvStopServer);
        auto sub = srv_msg.mutable_stop_server();
        app_->PostMessage2Service(srv_msg.SerializeAsString());
        return true;
    }

    bool PxRenderController::ReStart() {
        px::ServiceMessage srv_msg;
        srv_msg.set_type(ServiceMessageType::kSrvRestartServer);
        auto sub = srv_msg.mutable_restart_server();
        sub->set_work_dir(GetWorkDir().toStdString());
        sub->set_app_path(GetAppPath().toStdString());
        auto args = this->GetArgs();
        LOGI("Restart args:");
        for (auto& arg : args) {
            sub->add_args(arg);
            LOGI("{}", arg);
        }
        app_->PostMessage2Service(srv_msg.SerializeAsString());
        return true;
    }

    void PxRenderController::Exit() {

    }

    QString PxRenderController::GetWorkDir() {
        return QCoreApplication::applicationDirPath();
    }

    QString PxRenderController::GetAppPath() {
        QString current_path = QCoreApplication::applicationDirPath();
        current_path = current_path.append("/").append(kPxRenderName);
        return current_path;
    }

    std::vector<std::string> PxRenderController::GetArgs() {
        auto settings = PxSettings::Instance();
        // Always refresh from disk so restart picks up the latest capture device, etc.
        settings->Load();
        std::vector<std::string> args;
        args.push_back(std::format("--app_mode={}", "desktop"));
        args.push_back(std::format("--{}={}", kStEncoderSelectType, settings->encoder_select_type_));
        args.push_back(std::format("--{}={}", kStEncoderName, settings->encoder_name_));
        args.push_back(std::format("--{}={}", kStEncoderFormat, settings->GetEncoderFormat()));
        args.push_back(std::format("--{}={}", kStEncoderBitrate, settings->GetBitrate()));
        args.push_back(std::format("--{}={}", kStEncoderFPS, settings->GetFPS()));
        args.push_back(std::format("--encoder_resolution_type={}", (settings->IsResResizeEnabled() ? kResTypeResize : kResTypeOrigin)));
        args.push_back(std::format("--{}={}", kStEncoderWidth, settings->GetResWidth()));
        args.push_back(std::format("--{}={}", kStEncoderHeight, settings->GetResHeight()));
        args.push_back(std::format("--{}={}", kStCaptureAudio, settings->IsCaptureAudioEnabled()));
        args.push_back(std::format("--{}={}", kStCaptureAudioType, settings->capture_audio_type_));
        args.push_back(std::format("--{}={}", kStCaptureVideo, settings->capture_video_));
        args.push_back(std::format("--{}={}", kStCaptureVideoType, settings->capture_video_type_));
        args.push_back(std::format("--{}={}", kStWebSocketEnabled, settings->IsWebSocketEnabled()));
        args.push_back(std::format("--{}={}", kStNetworkListenPort, settings->GetRenderServerPort()));
        args.push_back(std::format("--{}={}", kStWebRTCEnabled, settings->webrtc_enabled_));
        args.push_back(std::format("--{}={}", kStUdpKcpEnabled, settings->udp_kcp_enabled_));
        // Capture audio always uses the OS default device inside the plugin; do not pass a device id.
        args.push_back(std::format("--{}={}", kStAppGamePath, ""));
        args.push_back(std::format("--{}={}", kStAppGameArgs, ""));
        args.push_back(std::format("--{}={}", kStDebugBlock, false));
        args.push_back(std::format("--{}={}", kStDeviceId, settings->GetDeviceId()));
        args.push_back(std::format("--{}={}", kStDeviceRandomPwd, settings->GetDeviceRandomPwd()));
        args.push_back(std::format("--{}={}", kStDeviceSafetyPwd, settings->GetDeviceSecurityPwd()));
        args.push_back(std::format("--panel_server_host={}", settings->GetPanelServerHost()));
        args.push_back(std::format("--panel_server_port={}", settings->GetPanelServerPort()));
        args.push_back(std::format("--service_server_host={}", settings->GetServiceServerHost()));
        args.push_back(std::format("--service_server_port={}", settings->GetServiceServerPort()));
        args.push_back(std::format("--{}={}", kStRelayServerHost, settings->GetRelayServerHost()));
        args.push_back(std::format("--{}={}", kStRelayServerPort, settings->GetRelayServerPort()));
        args.push_back(std::format("--{}={}", kStCanBeOperated, settings->IsBeingOperatedEnabled()));
        args.push_back(std::format("--{}={}", kStRelayEnabled, settings->IsRelayEnabled()));
        args.push_back(std::format("--language={}", (int)tcTrMgr()->GetSelectedLanguage()));
        args.push_back(std::format("--{}={}", kStLogFile, settings->log_file_));
        args.push_back(std::format("--appkey={}", grApp->GetAppkey()));
        return args;
    }

}
