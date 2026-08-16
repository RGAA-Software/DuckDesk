//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_WAS_AUDIO_CAPTURE_PLUGIN_H
#define PX_RENDER_WAS_AUDIO_CAPTURE_PLUGIN_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "px_render/plugin_interface/px_data_provider_plugin.h"

namespace px
{

    class IAudioCapture;

    class WasAudioCapturePlugin : public PxDataProviderPlugin {
    public:
        ~WasAudioCapturePlugin() override;

        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;

        bool OnCreate(const px::PxPluginParam &param) override;
        void OnCommand(const std::string &command) override;
        void StartProviding() override;
        void StopProviding() override;

        void SetAudioLoopbackProcessId(uint32_t pid) override;
        uint32_t GetAudioLoopbackProcessId() const override;
        bool IsProviding() const override;
        int GetLastStartError() const override;

    private:
        // 采集线程致命错误（如 AUDCLNT_E_DEVICE_INVALIDATED）后的自动重启：
        // stop callback 只投递重启请求，专用 worker 延迟后重新 StartProviding，
        // 固定 2s 间隔、无限重试（目标进程退出则放弃）。StopProviding/pid 变更/
        // 外部 Start 取消挂起的重启并清零重试状态。
        void OnFatalCaptureStop(uint32_t capture_pid);
        void RestartWorkerMain();
        // 以下两个函数调用时必须已持有 restart_mu_
        void ScheduleRestartLocked();
        void ResetRestartStateLocked();
        static bool IsProcessAlive(uint32_t pid);

        mutable std::mutex provide_mu_;
        int samples_ = 0;
        int channels_ = 0;
        int bits_ = 0;
        int last_start_error_ = 0;
        uint32_t loopback_process_id_ = 0;
        std::shared_ptr<IAudioCapture> audio_capture_ = nullptr;

        std::thread restart_thread_;
        std::mutex restart_mu_;
        std::condition_variable restart_cv_;
        bool restart_exit_ = false;        // guarded by restart_mu_
        bool restart_pending_ = false;     // guarded by restart_mu_
        int restart_pending_delay_ms_ = 0; // guarded by restart_mu_
        int restart_fail_count_ = 0;       // guarded by restart_mu_ (informational)
        // 每次 ResetRestartStateLocked 递增；worker 在真正重启前校验，避免与
        // 并发的 StopProviding/外部 Start 竞争出"取消后仍重启"
        uint64_t restart_generation_ = 0;  // guarded by restart_mu_
        std::atomic<bool> internal_restart_{false};
    };

}


PX_PLUGIN_EXPORT(px::WasAudioCapturePlugin)


#endif //PX_UDP_PLUGIN_H
