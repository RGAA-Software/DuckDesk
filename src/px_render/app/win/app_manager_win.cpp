//
// Created by RGAA on 2023-12-21.
//

#include "app_manager_win.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "game_process_identity.h"
#include "px_common/win32/unique_win_handle.h"
#include "px_common/process_util.h"
#include "px_common/string_util.h"
#include "px_common/file_util.h"
#include "px_common/log.h"
#include "rd_context.h"
#include "rd_app.h"
#include "network/net_message_maker.h"
#include "settings/rd_settings.h"
#include "app/app_messages.h"
#include "px_common/win32/process_helper.h"
#include "px_common/win32/win_helper.h"
#include "px_capture/process_loopback_support.h"
#include <shellapi.h>
#include <filesystem>
#include <sstream>
#include <chrono>
#include <utility>

#pragma comment(lib, "Shell32.lib")

namespace px
{

    constexpr auto kInjectorName = "px_gh_injector.exe";
    // 暂无 32 位版 px_gh.dll，32 位游戏明确拒绝注入（见 InjectDll）
    constexpr auto kX86DllName = "";
    constexpr auto kX64DllName = "px_gh.dll";

    // 注入失败固定重试间隔（不指数退避、不设次数上限，尽快出画面）
    constexpr int kInjectRetryIntervalMs = 100;
    // injected_ 置位后周期性存活检查：连续 3 次失败才重置（避免误伤偶发检测失败）
    constexpr int kInjectAliveMaxFailCount = 3;
    constexpr int kInjectAliveCheckIntervalMs = 1000;
    // gave_up 后探测游戏重启的间隔（目标消失/换 pid 则恢复注入）
    constexpr int kInjectGaveUpProbeIntervalMs = 3000;
    // 游戏看门狗：存活检查间隔 / 自动重启最小间隔（不限次数、永不放弃，同注入重试策略）
    constexpr int64_t kGameWatchdogCheckIntervalMs = 1000;
    constexpr int64_t kGameRestartMinIntervalMs = 5000;

    AppManagerWinImpl::AppManagerWinImpl(const std::shared_ptr<RdContext>& ctx)
        : AppManager(ctx), settings_(*RdSettings::Instance()) {}
    AppManagerWinImpl::~AppManagerWinImpl() { Exit(); }

    void AppManagerWinImpl::Init() {
        AppManager::Init();

        // 注入流程跑在独立 worker 线程（内部自带固定间隔重试/存活检查），消息线程只投递请求
        const auto self = std::static_pointer_cast<AppManagerWinImpl>(shared_from_this());
        const std::weak_ptr<AppManagerWinImpl> weak_self = self;
        inject_worker_ = std::make_shared<std::thread>([weak_self]() {
            if (const auto self = weak_self.lock()) {
                self->InjectWorkerLoop();
            }
        });

        if (settings_.capture_.IsVideoInnerCapture()) {
            state_msg_listener_->Listen<MsgTimer100>([weak_self](const MsgTimer100&) {
                const auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                self->context_->PostTask([weak_self]() {
                    const auto self = weak_self.lock();
                    if (!self || self->exiting_) {
                        return;
                    }
                    self->InjectCaptureDllIfNeeded();
                    if (self->target_pid_ > 0) {
                        auto infos = px::AppManagerWinImpl::SearchWindowByPid(self->target_pid_);
                        std::lock_guard window_lock(self->target_window_mutex_);
                        self->target_window_info_ = GetTargetWindowInfo(infos);
                    }
                });
            });
        } else {
            state_msg_listener_->Listen<MsgTimer2000>([weak_self](const MsgTimer2000&) {
                const auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                self->context_->PostTask([weak_self]() {
                    if (const auto self = weak_self.lock(); self && !self->exiting_) {
                        self->InjectCaptureDllIfNeeded();
                    }
                });
            });
        }
    }

    static std::string GetExeFolderPath() {
        std::vector<wchar_t> buffer(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length || length >= buffer.size()) {
            return {};
        }
        return PathToUTF8(std::filesystem::path{std::wstring(buffer.data(), length)}.parent_path());
    }

    bool AppManagerWinImpl::StartProcessWithHook() {
        const auto executable = NormalizeGameExecutable(PathFromUTF8(settings_.app_.game_path_));
        std::error_code error{};
        if (!executable || !std::filesystem::is_regular_file(*executable, error)) {
            LOGE("Owned App launch refused: configure an absolute executable file path.");
            return false;
        }
        target_pid_ = LaunchGameProcess(PathToUTF8(*executable), settings_.app_.game_arguments_);
        if (target_pid_ > 0) {
            MarkGameLaunched();
        }
        return target_pid_ > 0;
    }

    void AppManagerWinImpl::MarkGameLaunched() {
        // 看门狗据此确认游戏拉起过至少一次，并记录重启节流起点。
        game_ever_seen_ = true;
        last_game_restart_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void AppManagerWinImpl::NotifyGameStatus(px::GameStatusChanged::GameStatus status, const std::string& detail) {
        // 广播给所有已连接客户端（ws / rtc_local 插件路由），游戏死亡重启期间
        // 客户端无新帧，靠这个消息让用户知道在重启而不是卡死
        if (rdApp) {
            rdApp->PostNetMessage(NetMessageMaker::MakeGameStatusChanged(status, detail));
        }
    }

    bool AppManagerWinImpl::StartProcess() {
        if (settings_.capture_.IsVideoInnerCapture()) {
            return StartProcessWithHook();
        }
        target_pid_ = LaunchGameProcess(settings_.app_.game_path_, settings_.app_.game_arguments_);
        return target_pid_ > 0;
    }
    uint32_t AppManagerWinImpl::LaunchGameProcess(const std::string& executable, const std::string& arguments) {
        const auto launched = OwnedGameProcess::Launch(PathFromUTF8(executable), StringUtil::ToWString(arguments));
        if (!launched) {
            LOGE("Owned game launch failed: process was not admitted to a private Job.");
            return 0;
        }
        std::shared_ptr<OwnedGameProcess> previous{};
        {
            std::scoped_lock lock(game_owner_mutex_);
            if (exiting_) {
                return 0;
            }
            previous = std::exchange(owned_game_, launched);
        }
        if (previous) {
            previous->Stop();
        }
        LOGI("event=game.launch outcome=owned pid={} path={}", launched->RootPid(), executable);
        return launched->RootPid();
    }
    std::shared_ptr<UniqueWinHandle> AppManagerWinImpl::AcquireHookTarget(uint32_t pid) const {
        if (exiting_) {
            return {};
        }
        std::shared_ptr<OwnedGameProcess> owner{};
        {
            std::scoped_lock lock(game_owner_mutex_);
            owner = owned_game_;
        }
        const bool view = !settings_.app_.game_view_path_.empty();
        const auto expected = PathFromUTF8(view ? settings_.app_.game_view_path_ : settings_.app_.game_path_);
        return owner ? owner->Acquire(pid, expected, view) : std::shared_ptr<UniqueWinHandle>{};
    }
    bool AppManagerWinImpl::CanHookProcess(uint32_t pid) const {
        return static_cast<bool>(AcquireHookTarget(pid));
    }

    void AppManagerWinImpl::InjectCaptureDllIfNeeded() {
        // 只投递请求：真正的注入（含同步等待 injector 数秒）在 worker 线程上执行，
        // 避免 100ms 定时器在任务线程上阻塞
        if (this->injected_) {
            return;
        }
        inject_requested_ = true;
        inject_cv_.notify_all();
    }

    void AppManagerWinImpl::InjectWorkerLoop() {
        while (!inject_worker_exit_) {
            if (!settings_.capture_.IsVideoInnerCapture()) {
                std::unique_lock<std::mutex> lock(inject_mtx_);
                inject_cv_.wait_for(lock, std::chrono::milliseconds(1000));
                continue;
            }

            // 游戏看门狗：仅重启本次拥有的游戏进程树（内部 1s 节流）
            EnsureGameRunning();

            if (injected_) {
                // 已注入：低频检查目标进程存活且 DLL 仍映射，游戏崩溃重开后重新走注入流程
                std::this_thread::sleep_for(std::chrono::milliseconds(kInjectAliveCheckIntervalMs));
                if (inject_worker_exit_) {
                    break;
                }
                VerifyInjectedStillAlive();
                continue;
            }

            if (inject_gave_up_) {
                {
                    std::unique_lock<std::mutex> lock(inject_mtx_);
                    inject_cv_.wait_for(lock, std::chrono::milliseconds(kInjectGaveUpProbeIntervalMs));
                }
                if (inject_worker_exit_) {
                    break;
                }
                // 低频探测：游戏重启（旧 pid 消失 / 本次进程树退出）则恢复注入流程。
                // 32 位拒绝场景新 pid 会再次快速拒绝，由本分支 3s 间隔压着，可接受
                if (inject_gave_up_) {
                    ProbeGaveUpTargetGone();
                }
                continue;
            }

            if (!inject_requested_.exchange(false)) {
                std::unique_lock<std::mutex> lock(inject_mtx_);
                inject_cv_.wait_for(lock, std::chrono::milliseconds(500));
                continue;
            }

            const bool attempted = InjectCaptureDllForNormalApp();

            if (injected_ || inject_gave_up_ || inject_worker_exit_) {
                continue;
            }
            if (!attempted) {
                // 目标进程还没出现（游戏未启动/未加载完），不算注入失败，低频等待
                std::unique_lock<std::mutex> lock(inject_mtx_);
                inject_cv_.wait_for(lock, std::chrono::milliseconds(500));
                continue;
            }

            // 固定 100ms 间隔重试，不设上限——尽快出画面优先。仅打节流日志便于观察。
            // 权限不足（ACCESS_DENIED）同样持续重试——用户可能随后以管理员重启 Render。
            ++inject_attempts_;
            if (inject_attempts_ == 1 || (inject_attempts_ % 100) == 0) {
                LOGW("Inject capture dll: attempt {} failed, keep retrying every {}ms, game: {}", inject_attempts_, kInjectRetryIntervalMs,
                     settings_.app_.game_path_);
            }
            std::unique_lock<std::mutex> lock(inject_mtx_);
            inject_cv_.wait_for(lock, std::chrono::milliseconds(kInjectRetryIntervalMs));
        }
        LOGI("Inject worker loop exit.");
    }

    void AppManagerWinImpl::VerifyInjectedStillAlive() {
        uint32_t pid = target_pid_;
        if (pid <= 0) {
            return;
        }
        const auto owned_target = AcquireHookTarget(pid);
        const bool alive = static_cast<bool>(owned_target);
        bool dll_mapped = false;
        if (alive) {
            auto result = WinHelper::IsDllInjected(pid, kX86DllName, kX64DllName);
            dll_mapped = result.ok_ && result.value_;
        }
        if (alive && dll_mapped) {
            inject_alive_fail_count_ = 0;
            return;
        }
        // 连续失败才重置，避免"游戏正常但 DLL 检测偶发失败"导致误触发重注入
        ++inject_alive_fail_count_;
        if (inject_alive_fail_count_ < kInjectAliveMaxFailCount) {
            return;
        }
        LOGW("Target pid: {} gone or px_gh.dll unmapped (alive: {}, mapped: {}), will re-inject.", pid, alive, dll_mapped);
        inject_alive_fail_count_ = 0;
        injected_ = false;
        ResetInjectRetryState();
    }

    void AppManagerWinImpl::ResetInjectRetryState() {
        inject_attempts_ = 0;
        inject_gave_up_ = false;
    }

    void AppManagerWinImpl::ProbeGaveUpTargetGone() {
        if (!AcquireHookTarget(last_inject_target_pid_.load())) {
            ResetInjectRetryState();
        }
    }

    void AppManagerWinImpl::EnsureGameRunning() {
        if (!settings_.IsGameHookMode() || exiting_) {
            return;
        }
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now_ms - last_watchdog_check_ms_.load() < kGameWatchdogCheckIntervalMs) {
            return;
        }
        last_watchdog_check_ms_ = now_ms;
        std::shared_ptr<OwnedGameProcess> owner{};
        {
            std::scoped_lock lock(game_owner_mutex_);
            owner = owned_game_;
        }
        if (!owner || !game_ever_seen_) {
            return;
        }
        if (!settings_.app_.game_view_path_.empty()) {
            for (const auto& candidate : ProcessHelper::GetProcessList(false)) {
                if (AcquireHookTarget(candidate->pid_)) {
                    view_ever_seen_ = true;
                    return;
                }
            }
            if (!view_ever_seen_ && owner->HasLiveProcesses()) {
                return;
            }
        } else if (owner->HasLiveProcesses()) {
            return;
        }
        if (now_ms - last_game_restart_ms_.load() < kGameRestartMinIntervalMs) {
            return;
        }
        owner->Stop();
        last_game_restart_ms_ = now_ms;
        waiting_first_frame_ = true;
        view_ever_seen_ = false;
        injected_ = false;
        inject_alive_fail_count_ = 0;
        ResetInjectRetryState();
        NotifyGameStatus(px::GameStatusChanged::kGameRestarting, settings_.app_.game_path_);
        StartProcessWithHook();
    }

    bool AppManagerWinImpl::InjectCaptureDllForNormalApp() {
        if (target_pid_ <= 0) {
            return false;
        }

        auto processes = ProcessHelper::GetProcessList(false);
        ProcessInfoPtr target_process_info = nullptr;

        std::shared_ptr<UniqueWinHandle> owned_target{};
        for (const auto& candidate : processes) {
            if (!candidate->Valid()) {
                continue;
            }
            auto guard = AcquireHookTarget(candidate->pid_);
            if (!guard) {
                continue;
            }
            target_process_info = candidate;
            owned_target = std::move(guard);
            break;
        }
        if (!owned_target || !target_process_info) {
            return false;
        }
        LOGI("event=game.hook_gate outcome=allowed pid={} ownership=private_job path=matched", target_process_info->pid_);
        if (settings_.capture_.capture_video_type_ == Capture::kCaptureScreen) {
            AddFoundPid(target_process_info);
        }
        if (settings_.capture_.IsVideoInnerCapture()) {
            if (this->injected_) {
                return true;
            }
            auto result = WinHelper::IsDllInjected(target_process_info->pid_, kX86DllName, kX64DllName);
            auto process_exe_name = FileUtil::GetFileNameFromPath(target_process_info->exe_full_path_);
            if (result.ok_ && result.value_) {
                LOGI("Pid: {} for: {} is already injected....", target_process_info->pid_, process_exe_name);
                this->injected_ = true;
                // 与 VerifyInjectedStillAlive 监控的 target_pid_ 保持一致，
                // 否则它会盯着一个旧 pid 反复误判"DLL 被卸载"并重置注入状态
                target_pid_ = target_process_info->pid_;
                ResetInjectRetryState();
                return true;
            }
            LOGI("Not injected, will inject for pid: {}, exe: {}", target_process_info->pid_, process_exe_name);

            AddFoundPid(target_process_info);

            // Sync boot file BEFORE inject (SendAppMessage is async and races).
            if (rdApp) {
                rdApp->PrepareGameHookBoot(target_process_info->pid_);
            } else {
                LOGE("rdApp null, cannot write hook boot config before inject");
            }
            context_->SendAppMessage(MsgBeforeInject{
                .pid_ = target_process_info->pid_,
            });

            bool injected =
                InjectDll(target_process_info->pid_, target_process_info->thread_id_, target_process_info->is_x86_, kX86DllName, kX64DllName);
            // DllMain used to block >4s under loader lock; inject helper then
            // timed out even when the module was actually mapped. Treat mapped DLL as OK.
            if (!injected) {
                auto again = WinHelper::IsDllInjected(target_process_info->pid_, kX86DllName, kX64DllName);
                if (again.ok_ && again.value_) {
                    LOGW("Injector timed out/failed but px_gh.dll is mapped — treat as success");
                    injected = true;
                }
            }
            this->injected_ = injected;
            if (injected) {
                LOGI("Inject success for pid: {}, exe: {}", target_process_info->pid_, process_exe_name);
                target_pid_ = target_process_info->pid_;
                ResetInjectRetryState();
                MsgObsInjected msg_injected{};
                SteamAppPtr mock_app = SteamApp::Make();
                mock_app->exes_.push_back(settings_.app_.game_path_);
                msg_injected.steam_app_ = mock_app;
                msg_injected.pid_ = target_process_info->pid_;
                context_->SendAppMessage(msg_injected);
            } else {
                LOGE("Inject capture dll failed for pid: {}, is x86:{}, exe: {}", target_process_info->pid_, target_process_info->is_x86_,
                     process_exe_name);
            }
        }
        return true;
    }

    bool AppManagerWinImpl::InjectDll(uint32_t pid, uint32_t tid, bool is_x86, const std::string& x86_dll, const std::string& x64_dll) {
        const auto owned_target = AcquireHookTarget(pid);
        if (!owned_target) {
            LOGE("event=game.hook_gate outcome=rejected pid={} reason=ownership_or_path", pid);
            return false;
        }
        last_inject_target_pid_ = pid;
        // OBS inject-helper style: "<injector> <dll> <is_thread> <pid>"
        // Prefer the render exe folder (same as collect_dist) over process cwd.
        auto current_exe_path = GetExeFolderPath();
        if (current_exe_path.empty()) {
            current_exe_path = StringUtil::ToUTF8(std::filesystem::current_path().wstring());
        }
        auto injector_path = std::format("{}/{}", current_exe_path, kInjectorName);
        StringUtil::Replace(injector_path, "\\", "/");
        auto target_dll = std::format("{}/{}", current_exe_path, x64_dll);
        StringUtil::Replace(target_dll, "\\", "/");

        // 32 位目标明确拒绝：暂无 32 位 px_gh.dll，且 inject-library 会把
        // 64 位 LoadLibraryW 地址写进 WoW64 进程（行为未定义，可能崩游戏）。
        // 记 permanent failure，停止重试
        if (is_x86) {
            inject_gave_up_ = true;
            LOGE("暂不支持 32 位游戏, pid: {}, 注入已放弃（需要 32 位版 px_gh.dll）", pid);
            return false;
        }
        {
            const UniqueWinHandle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
            if (process) {
                BOOL wow64 = FALSE;
                bool target_is_x86 = IsWow64Process(process.get(), &wow64) && wow64;
                if (target_is_x86) {
                    inject_gave_up_ = true;
                    LOGE("暂不支持 32 位游戏, pid: {}, 注入已放弃（需要 32 位版 px_gh.dll）", pid);
                    return false;
                }
            }
        }

        // 权限/完整性检查：目标以管理员运行而 Render 为普通权限时，injector 内
        // open_process(PROCESS_ALL_ACCESS) 永远失败，这里提前探测给出明确报错。
        // 此类失败与其它失败一样持续重试（用户可能随后以管理员重启 Render）
        {
            const UniqueWinHandle process{OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid)};
            if (!process) {
                DWORD err = GetLastError();
                if (err == ERROR_ACCESS_DENIED) {
                    // 日志节流：同一报错最多每 5s 一条
                    static std::atomic<int64_t> s_last_denied_log_ms{0};
                    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    if (now_ms - s_last_denied_log_ms.load() >= 5000) {
                        s_last_denied_log_ms = now_ms;
                        LOGE("OpenProcess(PROCESS_ALL_ACCESS) denied for pid: {}, "
                             "游戏以管理员权限运行，请以管理员权限运行 Render", pid);
                    }
                } else {
                    LOGW("OpenProcess failed for pid: {}, err: {}", pid, err);
                }
                return false;
            }
        }

        LOGI("Inject: {} {} pid: {}, tid: {}", injector_path, target_dll, pid, tid);

        std::vector<std::string> args;
        args.push_back(target_dll);
        args.emplace_back(tid == 0 ? "0" : "1"); // (0 / 1)
        args.push_back(std::to_string(pid));
        auto inject_result = ProcessUtil::StartProcessAndWait(injector_path, args);
        return inject_result;
    }

    void AppManagerWinImpl::Exit() {
        if (exiting_.exchange(true)) {
            return;
        }
        AppManager::Exit();
        inject_worker_exit_ = true;
        inject_cv_.notify_all();
        if (inject_worker_ && inject_worker_->joinable()) {
            inject_worker_->join();
        }
        inject_worker_.reset();
        CloseCurrentApp();
    }

    void AppManagerWinImpl::OnCapturedVideoFrame() {
        if (waiting_first_frame_.exchange(false)) {
            NotifyGameStatus(px::GameStatusChanged::kGameRunning, "");
        }
    }

    void* AppManagerWinImpl::GetWindowHandle() {
        std::lock_guard lock(target_window_mutex_);
        return reinterpret_cast<void*>(target_window_info_.win_handle);
    }

    std::optional<OwnedGameTextTarget> AppManagerWinImpl::AcquireTextTarget() const {
        const auto pid = static_cast<std::uint32_t>(target_pid_.load());
        const auto process = AcquireHookTarget(pid);
        if (!process) {
            return std::nullopt;
        }
        std::lock_guard lock(target_window_mutex_);
        DWORD window_pid{};
        if (!IsWindow(reinterpret_cast<HWND>(target_window_info_.win_handle)) ||
            !GetWindowThreadProcessId(reinterpret_cast<HWND>(target_window_info_.win_handle), &window_pid) || window_pid != pid) {
            return std::nullopt;
        }
        CaptureTextCommand command{};
        command.target_pid = pid;
        command.root_window = target_window_info_.win_handle;
        return OwnedGameTextTarget{std::move(command), process};
    }

    void AppManagerWinImpl::CloseCurrentApp() {
        std::shared_ptr<OwnedGameProcess> owner{};
        {
            std::scoped_lock lock(game_owner_mutex_);
            owner = std::move(owned_game_);
        }
        if (owner) { owner->Stop(); }
        found_process_info_.clear();
    }

    WindowInfos AppManagerWinImpl::SearchWindowByPid(uint32_t pid) {
        auto infos = ProcessHelper::GetWindowInfoByPid(pid, 256);
        if (!infos.infos.empty()) {
            for (auto& info : infos.infos) {
                //LOGI("pid : {}, exe : {}, title : {}, class : {}",
                //         info.pid, StringUtil::ToUTF8(info.exe_name).c_str(), StringUtil::ToUTF8(info.title).c_str(), info.claxx );
                auto size = info.GetWindowSize();
            }
        }
        return infos;
    }

    WindowInfo AppManagerWinImpl::GetTargetWindowInfo(const WindowInfos& infos) {
        WindowInfo info;
        if (infos.infos.empty()) {
            return info;
        }

        for (const auto& wif : infos.infos) {
            auto size = wif.GetWindowSize();
            if (size.first <= 10 && size.second <= 10) {
                continue;
            }
            info = wif;
            break;
        }

        if (info.win_handle) {
            //LOG_INFO("Old handle : %p,", info.win_handle);
            auto handle = GetParent(reinterpret_cast<HWND>(info.win_handle));
            while (handle) {
                info.win_handle = reinterpret_cast<std::uintptr_t>(handle);
                handle = GetParent(handle);
            }
            //LOG_INFO("New handle : %p,", info.win_handle);
        }

        return info;
    }

    void AppManagerWinImpl::AddFoundPid(const ProcessInfoPtr& target_pi) {
        bool exist = false;
        for (auto& pi : found_process_info_) {
            if (pi->pid_ == target_pi->pid_) {
                exist = true;
            }
        }
        if (!exist) {
            found_process_info_.push_back(target_pi);
        }
        //LOGI("found pid count: {}", found_process_info_.size());
    }

}
