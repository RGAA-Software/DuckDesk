//
// Created RGAA on 15/11/2024.
//

#include "was_audio_capture_plugin.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>

#include "audio_capture.h"
#include "gr_render/plugins/plugin_ids.h"
#include "miniaudio_audio_capture.h"
#include "process_loopback_audio_capture.h"
#include "tc_common_new/log.h"
#include "tc_common_new/memory_stat.h"
#include "gr_render/plugin_interface/gr_plugin_events.h"
#include "gr_render/plugin_interface/gr_plugin_context.h"

namespace tc
{
    namespace {
        // 致命错误自动重启：2s 起步指数退避到 30s，连续失败 5 次放弃
        constexpr int kRestartDelayStartMs = 2000;
        constexpr int kRestartDelayMaxMs = 30000;
        constexpr int kRestartMaxConsecutiveFails = 5;
    }

    WasAudioCapturePlugin::~WasAudioCapturePlugin() {
        // 停掉重启 worker 再析构成员；worker 可能正在 StartProviding（最长
        // 等待一次 activate 超时），join 保证不碰已析构的状态
        {
            std::lock_guard lock(restart_mu_);
            restart_exit_ = true;
            restart_pending_ = false;
        }
        restart_cv_.notify_all();
        if (restart_thread_.joinable()) {
            restart_thread_.join();
        }
    }

    std::string WasAudioCapturePlugin::GetPluginId() {
        return kWasAudioCapturePluginId;
    }

    std::string WasAudioCapturePlugin::GetPluginName() {
        return "MiniAudio";
    }

    std::string WasAudioCapturePlugin::GetVersionName() {
        return "1.3.0";
    }

    uint32_t WasAudioCapturePlugin::GetVersionCode() {
        return 130;
    }

    void WasAudioCapturePlugin::On1Second() {
#if MEMORY_STST_ON
        plugin_context_->PostWorkTask([=, this]() {
            auto info = MemoryStat::Instance()->GetStatInfo();
            LOGI("Memory usage: {}", info.Dump());
        });
#endif
    }

    std::string WasAudioCapturePlugin::GetPluginDescription() {
        return "MiniAudio WASAPI loopback (desktop default mix, or per-PID process-loopback)";
    }

    bool WasAudioCapturePlugin::OnCreate(const tc::GrPluginParam& param) {
        GrDataProviderPlugin::OnCreate(param);
        MemoryStat::Instance();
        restart_thread_ = std::thread([this] { RestartWorkerMain(); });
        LOGI("[WasAudioCapturePlugin] OnCreate");
        return true;
    }

    void WasAudioCapturePlugin::OnCommand(const std::string& command) {
        LOGI("[WasAudioCapturePlugin] OnCommand: {}", command);
    }

    void WasAudioCapturePlugin::SetAudioLoopbackProcessId(uint32_t pid) {
        std::lock_guard lock(provide_mu_);
        loopback_process_id_ = pid;
        {
            // pid 变更：取消挂起的自动重启并清零退避（新目标是全新会话）
            std::lock_guard rst_lock(restart_mu_);
            ResetRestartStateLocked();
        }
        LOGI("[WasAudioCapturePlugin] SetAudioLoopbackProcessId pid={}", pid);
    }

    uint32_t WasAudioCapturePlugin::GetAudioLoopbackProcessId() const {
        std::lock_guard lock(provide_mu_);
        return loopback_process_id_;
    }

    bool WasAudioCapturePlugin::IsProviding() const {
        std::lock_guard lock(provide_mu_);
        return audio_capture_ != nullptr;
    }

    int WasAudioCapturePlugin::GetLastStartError() const {
        std::lock_guard lock(provide_mu_);
        return last_start_error_;
    }

    void WasAudioCapturePlugin::StartProviding() {
        std::lock_guard lock(provide_mu_);
        last_start_error_ = 0;
        if (!internal_restart_.load()) {
            // 外部 Start（流重连 / MsgObsInjected 等）：新会话，取消挂起的自动
            // 重启并清零退避；worker 内部重启走 internal_restart_ 跳过此重置
            std::lock_guard rst_lock(restart_mu_);
            ResetRestartStateLocked();
        }
        LOGI("[WasAudioCapturePlugin] StartProviding, audio_enabled={}, loopback_pid={}",
             sys_settings_.audio_enabled_, loopback_process_id_);

        if (audio_capture_) {
            LOGW("[WasAudioCapturePlugin] previous capture still alive, stopping it first");
            audio_capture_->Stop();
            audio_capture_.reset();
        }

        if (loopback_process_id_ != 0) {
            // Keep native WASAPI process-loopback for production (proven).
            // MiniAudio PID path is patched (ActivateAudioInterfaceAsync); see
            // test_miniaudio_pid_loopback — switch only after more soak testing.
            audio_capture_ = ProcessLoopbackAudioCapture::Make(loopback_process_id_);
        } else {
            audio_capture_ = MiniAudioCapture::Make();
        }
        if (!audio_capture_) {
            last_start_error_ = -1;
            LOGE("[WasAudioCapturePlugin] MiniAudioCapture::Make failed");
            return;
        }

        audio_capture_->RegisterFormatCallback([=, this](int samples, int channels, int bits) {
            this->samples_ = samples;
            this->channels_ = channels;
            this->bits_ = bits;
            LOGI("[WasAudioCapturePlugin] format ready: {}Hz {}ch {}bit", samples, channels, bits);
        });

        audio_capture_->RegisterDataCallback([=, this](const std::shared_ptr<Data>& data) {
            if (!sys_settings_.audio_enabled_) {
                return;
            }
            if (!data || data->Size() <= 0) {
                return;
            }
            auto event = std::make_shared<GrPluginRawAudioFrameEvent>();
            event->full_data_ = data;
            event->sample_rate_ = this->samples_;
            event->channels_ = this->channels_;
            event->bits_ = this->bits_;
            CallbackEvent(event);
        });

        audio_capture_->RegisterSplitDataCallback([=, this](const auto& left, const auto& right) {
            if (!sys_settings_.audio_enabled_) {
                return;
            }
            auto event = std::make_shared<GrPluginSplitRawAudioFrameEvent>();
            event->left_ch_data_ = left;
            event->right_ch_data_ = right;
            event->sample_rate_ = this->samples_;
            event->channels_ = this->channels_;
            event->bits_ = this->bits_;
            CallbackEvent(event);
        });

        const uint32_t capture_pid = loopback_process_id_;
        IAudioCapture* cap_raw = audio_capture_.get();
        audio_capture_->RegisterStopCallback([=, this]() {
            // Fires on Stop()/StopProviding() (normal stop), and from the capture
            // thread itself on a fatal device error (e.g. AUDCLNT_E_DEVICE_INVALIDATED).
            // The capture object is guaranteed alive here: the fatal path notifies
            // on the capture thread, which Stop() joins before the plugin resets it.
            // Do NOT take provide_mu_ here — StopProviding holds it while Stop()
            // invokes this callback synchronously.
            if (cap_raw->IsFatalStop()) {
                this->OnFatalCaptureStop(capture_pid);
            } else {
                LOGW("[WasAudioCapturePlugin] audio capture stopped (normal), pid={}", capture_pid);
            }
        });

        const int start_ret = audio_capture_->Start();
        last_start_error_ = start_ret;
        if (start_ret != 0) {
            LOGE("[WasAudioCapturePlugin] Start failed, ret={}, pid={}", start_ret, loopback_process_id_);
            audio_capture_.reset();
            return;
        }
        LOGI("[WasAudioCapturePlugin] StartProviding OK ({})",
             loopback_process_id_ ? "PID process-loopback" : "default device loopback");
    }

    void WasAudioCapturePlugin::StopProviding() {
        std::lock_guard lock(provide_mu_);
        LOGI("[WasAudioCapturePlugin] StopProviding");
        {
            // 主动停止（含 Render 退出路径）：取消挂起的自动重启并清零退避
            std::lock_guard rst_lock(restart_mu_);
            ResetRestartStateLocked();
        }
        if (!audio_capture_) {
            LOGW("[WasAudioCapturePlugin] StopProviding: capture already null");
            return;
        }
        audio_capture_->Stop();
        audio_capture_.reset();
    }

    void WasAudioCapturePlugin::OnFatalCaptureStop(uint32_t capture_pid) {
        // 只在 process-loopback 路径做自动重启（pid=0 是 miniaudio 默认设备采集，
        // 其 stop 不区分致命/正常，不进入此分支）
        if (capture_pid == 0) {
            return;
        }
        std::lock_guard lock(restart_mu_);
        if (restart_exit_) {
            return;
        }
        if (ScheduleRestartLocked()) {
            LOGW("[WasAudioCapturePlugin] capture fatal stop, pid={}, auto-restart #{} in {}ms",
                 capture_pid, restart_fail_count_, restart_pending_delay_ms_);
        } else {
            LOGE("[WasAudioCapturePlugin] capture fatal stop, pid={}, give up after {} "
                 "consecutive failures", capture_pid, kRestartMaxConsecutiveFails);
        }
    }

    bool WasAudioCapturePlugin::ScheduleRestartLocked() {
        ++restart_fail_count_;
        if (restart_fail_count_ > kRestartMaxConsecutiveFails) {
            restart_pending_ = false;
            return false;
        }
        restart_pending_ = true;
        restart_pending_delay_ms_ = restart_delay_ms_;
        restart_delay_ms_ = (std::min)(restart_delay_ms_ * 2, kRestartDelayMaxMs);
        restart_cv_.notify_all();
        return true;
    }

    void WasAudioCapturePlugin::ResetRestartStateLocked() {
        restart_pending_ = false;
        restart_fail_count_ = 0;
        restart_delay_ms_ = kRestartDelayStartMs;
        ++restart_generation_;
        restart_cv_.notify_all();
    }

    bool WasAudioCapturePlugin::IsProcessAlive(uint32_t pid) {
        if (pid == 0) {
            return false;
        }
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) {
            return false;
        }
        DWORD exit_code = 0;
        const bool alive = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
        CloseHandle(process);
        return alive;
    }

    void WasAudioCapturePlugin::RestartWorkerMain() {
        std::unique_lock lock(restart_mu_);
        while (!restart_exit_) {
            restart_cv_.wait(lock, [this] { return restart_exit_ || restart_pending_; });
            if (restart_exit_) {
                break;
            }
            const int delay = restart_pending_delay_ms_;
            const uint64_t gen = restart_generation_;
            // 延迟窗口内可取消：StopProviding / pid 变更 / 外部 Start / 析构
            const bool cancelled = restart_cv_.wait_for(
                lock, std::chrono::milliseconds(delay),
                [this] { return restart_exit_ || !restart_pending_; });
            if (restart_exit_) {
                break;
            }
            if (cancelled || gen != restart_generation_) {
                continue;
            }
            restart_pending_ = false;
            lock.unlock();

            const uint32_t pid = GetAudioLoopbackProcessId();
            if (pid == 0 || !IsProcessAlive(pid)) {
                // 目标进程已退出（游戏关闭/重启中），重启无意义，直接放弃
                LOGE("[WasAudioCapturePlugin] auto-restart aborted: target pid={} gone", pid);
                lock.lock();
                ResetRestartStateLocked();
                continue;
            }
            LOGI("[WasAudioCapturePlugin] auto-restart capture, pid={}", pid);
            internal_restart_ = true;
            StartProviding();
            internal_restart_ = false;
            const int start_err = GetLastStartError();
            lock.lock();
            if (start_err != 0 && !restart_exit_) {
                // Start 本身失败（如 activate 超时）也算一次失败，继续退避重试
                if (ScheduleRestartLocked()) {
                    LOGW("[WasAudioCapturePlugin] auto-restart Start failed err={}, pid={}, "
                         "retry #{} in {}ms",
                         start_err, pid, restart_fail_count_, restart_pending_delay_ms_);
                } else {
                    LOGE("[WasAudioCapturePlugin] auto-restart give up after {} consecutive "
                         "failures, pid={}", kRestartMaxConsecutiveFails, pid);
                }
            }
        }
        LOGI("[WasAudioCapturePlugin] restart worker exit");
    }
}
