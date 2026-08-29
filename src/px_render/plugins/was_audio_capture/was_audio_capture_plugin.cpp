//
// Created RGAA on 15/11/2024.
//

#include "was_audio_capture_plugin.h"

#include "was_audio_capture_runtime.h"
#include "px_common_new/log.h"
#include "px_common_new/memory_stat.h"
#include "px_render/plugin_interface/px_plugin_context.h"
#include "px_render/plugins/plugin_ids.h"

namespace px {

WasAudioCapturePlugin::~WasAudioCapturePlugin() {
    if (runtime_) {
        runtime_->Shutdown();
        runtime_.reset();
    }
}

std::string WasAudioCapturePlugin::GetPluginId() {
    return kWasAudioCapturePluginId;
}

std::string WasAudioCapturePlugin::GetPluginName() {
    return "MiniAudio";
}

std::string WasAudioCapturePlugin::GetVersionName() {
    return "1.4.0";
}

uint32_t WasAudioCapturePlugin::GetVersionCode() {
    return 140;
}

void WasAudioCapturePlugin::On1Second() {
#if MEMORY_STST_ON
    const auto context = plugin_context_;
    if (context) {
        context->PostWorkTask([]() {
            const auto info = MemoryStat::Instance()->GetStatInfo();
            LOGI("Memory usage: {}", info.Dump());
        });
    }
#endif
}

std::string WasAudioCapturePlugin::GetPluginDescription() {
    return "MiniAudio WASAPI loopback (desktop default mix, or per-PID process-loopback)";
}

bool WasAudioCapturePlugin::OnCreate(const px::PxPluginParam& param) {
    if (!PxDataProviderPlugin::OnCreate(param)) {
        return false;
    }
    MemoryStat::Instance();
    runtime_ = WasAudioCaptureRuntime::Make();
    RefreshRuntimeDelivery();
    LOGI("[WasAudioCapturePlugin] OnCreate");
    return true;
}

bool WasAudioCapturePlugin::OnStop() {
    if (runtime_) {
        runtime_->StopProviding();
    }
    return PxDataProviderPlugin::OnStop();
}

bool WasAudioCapturePlugin::OnDestroy() {
    if (runtime_) {
        runtime_->Shutdown();
        runtime_.reset();
    }
    return PxDataProviderPlugin::OnDestroy();
}

void WasAudioCapturePlugin::OnCommand(const std::string& command) {
    LOGI("[WasAudioCapturePlugin] OnCommand: {}", command);
}

void WasAudioCapturePlugin::OnSyncPluginSettingsInfo(
    const PxPluginSettingsInfo& settings) {
    PxDataProviderPlugin::OnSyncPluginSettingsInfo(settings);
    if (runtime_) {
        runtime_->SetAudioEnabled(sys_settings_.audio_enabled_);
        RefreshRuntimeDelivery();
    }
}

void WasAudioCapturePlugin::RefreshRuntimeDelivery() {
    if (!runtime_) {
        return;
    }
    runtime_->ConfigureDelivery(
        plugin_context_, event_cbk_, sys_settings_.audio_enabled_);
}

void WasAudioCapturePlugin::SetAudioLoopbackProcessId(uint32_t pid) {
    if (runtime_) {
        runtime_->SetLoopbackProcessId(pid);
    }
}

uint32_t WasAudioCapturePlugin::GetAudioLoopbackProcessId() const {
    return runtime_ ? runtime_->GetLoopbackProcessId() : 0;
}

bool WasAudioCapturePlugin::IsProviding() const {
    return runtime_ && runtime_->IsProviding();
}

int WasAudioCapturePlugin::GetLastStartError() const {
    return runtime_ ? runtime_->GetLastStartError() : -1;
}

void WasAudioCapturePlugin::StartProviding() {
    if (!runtime_) {
        LOGE("[WasAudioCapturePlugin] StartProviding called before OnCreate");
        return;
    }
    RefreshRuntimeDelivery();
    runtime_->StartProviding();
}

void WasAudioCapturePlugin::StopProviding() {
    if (runtime_) {
        runtime_->StopProviding();
    }
}

}  // namespace px
