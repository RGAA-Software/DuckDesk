//
// Created by RGAA on 2023-12-21.
//

#include "app_manager_win.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "tc_common_new/process_util.h"
#include "tc_common_new/string_util.h"
#include "tc_common_new/file_util.h"
#include "tc_common_new/log.h"
#include "rd_context.h"
#include "rd_app.h"
#include "network/net_message_maker.h"
#include "settings/rd_settings.h"
#include "app/steam_game.h"
#include "app/app_messages.h"
#include "tc_common_new/win32/process_helper.h"
#include "tc_common_new/win32/win_helper.h"
#include "tc_capture_new/process_loopback_support.h"
#include <shellapi.h>
#include <filesystem>
#include <sstream>
#include <chrono>

#pragma comment(lib, "Shell32.lib")

namespace tc
{

    constexpr auto kInjectorName = "tc_graphics_util.exe";
    // 暂无 32 位版 tc_graphics.dll，32 位游戏明确拒绝注入（见 InjectDll）
    constexpr auto kX86DllName = "";
    constexpr auto kX64DllName = "tc_graphics.dll";

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
    // steam 游戏冷启动慢（拉起 url 后进程可能几十秒才出现），这段时间内不得重复拉 url
    constexpr int64_t kSteamRelaunchGraceMs = 60000;

    AppManagerWinImpl::AppManagerWinImpl(const std::shared_ptr<RdContext>& ctx) : AppManager(ctx) {
        settings_ = RdSettings::Instance();
    }

    AppManagerWinImpl::~AppManagerWinImpl() {
        inject_worker_exit_ = true;
        inject_cv_.notify_all();
        if (inject_worker_ && inject_worker_->joinable()) {
            inject_worker_->join();
        }
    }

    void AppManagerWinImpl::Init() {
        AppManager::Init();

        steam_game_ = std::make_shared<SteamGame>(context_);
        //steam_game_->RequestSteamGames();

        // 重启后第一帧到达 → 通知客户端"游戏已恢复"（waiting_first_frame_ 由看门狗置位）
        msg_listener_->Listen<CaptureVideoFrame>([=, this](const auto& msg) {
            if (waiting_first_frame_.exchange(false)) {
                NotifyGameStatus(tc::GameStatusChanged::kGameRunning, "");
            }
        });

        // 注入流程跑在独立 worker 线程（内部自带固定间隔重试/存活检查），消息线程只投递请求
        inject_worker_ = std::make_shared<std::thread>([=, this]() {
            this->InjectWorkerLoop();
        });

        if (settings_->capture_.IsVideoInnerCapture()) {
            msg_listener_->Listen<MsgTimer100>([=, this](const auto &msg) {
                context_->PostTask([=, this]() {
                    this->InjectCaptureDllIfNeeded();
                    if (target_pid_ > 0) {
                        auto infos = tc::AppManagerWinImpl::SearchWindowByPid(target_pid_);
                        target_window_info_ = GetTargetWindowInfo(infos);
                    }
                });
            });
        } else {
            msg_listener_->Listen<MsgTimer2000>([=, this](const auto &msg) {
                context_->PostTask([=, this]() {
                    this->InjectCaptureDllIfNeeded();
                });
            });
        }
    }

    static std::string GetExeFolderPath() {
        wchar_t file_path[MAX_PATH + 1] = {0};
        GetModuleFileNameW(nullptr, file_path, MAX_PATH);
        (wcsrchr(file_path, L'\\'))[0] = 0;
        return StringUtil::ToUTF8(file_path);
    }

    bool AppManagerWinImpl::StartProcessWithHook() {
        auto config_exe_path = settings_->app_.game_path_;
        bool is_steam_url = settings_->app_.IsSteamUrl();
        //StringUtil::ToWString(config_exe_path);
        //auto exe_path = std::filesystem::u8path(config_exe_path);
        if (!std::filesystem::exists(StringUtil::ToWString(config_exe_path)) && !is_steam_url) {
            LOGE("Exe not exists: {}", config_exe_path);
            return false;
        }

        std::wstring exec = StringUtil::ToWString(config_exe_path);
        std::wstring arguments = StringUtil::ToWString(settings_->app_.game_arguments_);
        std::wstring x86_dll;
        std::wstring x64_dll = L"tc_graphics.dll";

        InjectParams inject_params {};
        std::string folder_path = GetExeFolderPath();
        if (StringUtil::CopyCStringToArray(inject_params.host_exe_folder, folder_path)) {
            LOGW("host_exe_folder truncated, path: {}, src_len: {}, dst_len: {}",
                 folder_path, folder_path.size(), sizeof(inject_params.host_exe_folder));
        }
        inject_params.listening_port = settings_->transmission_.listening_port_;
        // Kept in sync with hook_boot AppSharedMessage::enable_hook_audio_ (OBS inject path uses boot file).
        // GODESK_FORCE_HOOK_AUDIO=1 forces in-process hook even when PID loopback is available.
        inject_params.enable_hook_audio = PreferProcessLoopbackCapture() ? 0u : 1u;

        // steam prefix
        if (is_steam_url) {
            ShellExecuteW(nullptr, nullptr, exec.c_str(), nullptr, nullptr , SW_SHOW );
            MarkGameLaunched();
            return true;
        }

#if 0
        // use easyhook to start
        auto result = RhCreateAndInject(
                const_cast<wchar_t *>(exec.data()),
                const_cast<wchar_t *>(arguments.data()),
                0,
                EASYHOOK_INJECT_DEFAULT,
                const_cast<wchar_t *>(x86_dll.data()),
                const_cast<wchar_t *>(x64_dll.data()),
                &inject_params,
                sizeof(InjectParams),
                &target_pid_
        );
        if (result >= 0) {
            LOGI("Start & Hook success...");
            return true;
        } else {
            LOGI("Start & Hook failed: {}", (int)result);
            return false;
        }
#endif
        std::vector<std::string> args;
        auto u8_exec = StringUtil::ToUTF8(exec);
        std::istringstream iss(settings_->app_.game_arguments_);
        std::string arg;
        while (iss >> arg) {
            args.push_back(arg);
        }

        LOGI("we will use normal method to start, exe: {}, args: [{}]",
             u8_exec, settings_->app_.game_arguments_);
        target_pid_ = LaunchGameProcess(u8_exec, args);
        LOGI("After started, the pid is: {}", target_pid_.load());
        if (target_pid_ > 0) {
            MarkGameLaunched();
        }
        return target_pid_ > 0;
    }

    void AppManagerWinImpl::MarkGameLaunched() {
        // 看门狗据此确认"游戏拉起过至少一次"，并以其作为 steam 冷启动宽限的计时起点
        game_ever_seen_ = true;
        last_game_restart_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void AppManagerWinImpl::NotifyGameStatus(tc::GameStatusChanged::GameStatus status, const std::string& detail) {
        // 广播给所有已连接客户端（ws / rtc_local 插件路由），游戏死亡重启期间
        // 客户端无新帧，靠这个消息让用户知道在重启而不是卡死
        if (rdApp) {
            rdApp->PostNetMessage(NetMessageMaker::MakeGameStatusChanged(status, detail));
        }
    }

    bool AppManagerWinImpl::StartProcess() {
        BOOL ret = false;
        auto config_exe_path = settings_->app_.game_path_;
        bool is_steam_url = settings_->app_.IsSteamUrl();
        std::wstring exec = StringUtil::ToWString(config_exe_path);
        std::wstring arguments = StringUtil::ToWString(settings_->app_.game_arguments_);

        // steam url
        if (is_steam_url) {
            if (!steam_game_->Ready()) {
                LOGW("Steam not ready, will request again...");
                steam_game_->RequestSteamGames();
            }
            const std::wstring& target_exec = exec;
            if (config_exe_path.find("bigpicture") != std::string::npos) {
                // steam big picture mode
                // target_exec = "steam.exe steam://open/bigpicture'"
                auto big_pic_mode = "steam://open/bigpicture";
                auto steam_exe_path = std::format(R"({} {})", steam_game_->GetSteamExePath(), big_pic_mode);
                LOGI("Steam start in big picture mode, steam exe: {}, mode: {}", steam_exe_path, big_pic_mode);
                auto steam_exe_folder = steam_game_->GetSteamInstalledPath();
                ProcessUtil::StartProcessInWorkDir(steam_exe_folder, steam_exe_path, {});
            } else {
                LOGI("Steam url: {}", config_exe_path);
                ShellExecuteW(nullptr, nullptr, target_exec.c_str(), nullptr, nullptr , SW_SHOW );
            }
            return true;
        }

        if (!std::filesystem::exists(StringUtil::ToWString(config_exe_path)) && !is_steam_url) {
            LOGE("Exe not exists: {}", config_exe_path);
            return false;
        }

        std::vector<std::string> args;
        auto u8_exec = StringUtil::ToUTF8(exec);
        std::istringstream iss(settings_->app_.game_arguments_);
        std::string arg;
        while (iss >> arg) {
            args.push_back(arg);
        }
        LOGI("we will use normal method to start, exe: {}", u8_exec);
        target_pid_ = LaunchGameProcess(u8_exec, args);
        return ret;
    }

    uint32_t AppManagerWinImpl::LaunchGameProcess(const std::string& u8_exec, const std::vector<std::string>& args) {
        uint32_t pid = ProcessUtil::StartProcessAsCurrentUser(u8_exec, args);
        if (pid == 0) {
            LOGW("StartProcessAsCurrentUser failed, fallback to CreateProcess (SYSTEM context)");
            pid = ProcessUtil::StartProcess(u8_exec, args, true, false);
        }
        return pid;
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
            if (!settings_->capture_.IsVideoInnerCapture()) {
                std::unique_lock<std::mutex> lock(inject_mtx_);
                inject_cv_.wait_for(lock, std::chrono::milliseconds(1000));
                continue;
            }

            // 游戏看门狗：游戏死了自动拉起 + 收养外部重启的新 pid（内部 1s 节流）
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
                // 低频探测：游戏重启（旧 pid 消失 / 同 exe 新 pid）则恢复注入流程。
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

            bool is_steam_url = settings_->app_.IsSteamUrl();
            bool attempted = !is_steam_url ? InjectCaptureDllForNormalApp()
                                           : InjectCaptureDllForSteamApp();

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
                LOGW("Inject capture dll: attempt {} failed, keep retrying every {}ms, game: {}",
                     inject_attempts_, kInjectRetryIntervalMs, settings_->app_.game_path_);
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
        bool alive = false;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            DWORD exit_code = 0;
            alive = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
            CloseHandle(process);
        }
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
        LOGW("Target pid: {} gone or tc_graphics.dll unmapped (alive: {}, mapped: {}), will re-inject.",
             pid, alive, dll_mapped);
        inject_alive_fail_count_ = 0;
        injected_ = false;
        ResetInjectRetryState();
    }

    void AppManagerWinImpl::ResetInjectRetryState() {
        inject_attempts_ = 0;
        inject_gave_up_ = false;
    }

    void AppManagerWinImpl::ProbeGaveUpTargetGone() {
        const uint32_t pid = last_inject_target_pid_.load();
        bool recover = false;
        if (pid > 0) {
            bool alive = false;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (process) {
                DWORD exit_code = 0;
                alive = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
                CloseHandle(process);
            }
            if (!alive) {
                LOGW("Inject gave-up target pid: {} gone (game restarted?), resume injection.", pid);
                recover = true;
            }
        }
        if (!recover && !settings_->app_.IsSteamUrl() && !settings_->app_.game_path_.empty()) {
            // 普通 app：同 exe 出现了新 pid（换了实例），也恢复注入；steam 场景靠
            // 上面的"旧 pid 消失"覆盖，恢复后由正常注入流程重新发现新 pid
            const auto target_exe_name = FileUtil::GetFileNameFromPath(settings_->app_.game_path_);
            for (const auto& process : ProcessHelper::GetProcessList(false)) {
                if (process->pid_ != pid &&
                    FileUtil::GetFileNameFromPath(process->exe_full_path_) == target_exe_name) {
                    LOGW("Found new pid: {} for exe: {} (gave-up pid: {}), resume injection.",
                         process->pid_, target_exe_name, pid);
                    recover = true;
                    break;
                }
            }
        }
        if (recover) {
            ResetInjectRetryState();
        }
    }

    bool AppManagerWinImpl::IsProcessAlive(uint32_t pid) {
        if (pid <= 0) {
            return false;
        }
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) {
            return false;
        }
        DWORD exit_code = 0;
        bool alive = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
        CloseHandle(process);
        return alive;
    }

    void AppManagerWinImpl::EnsureGameRunning() {
        if (!settings_->IsGameHookMode()) {
            return;
        }
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now_ms - last_watchdog_check_ms_.load() < kGameWatchdogCheckIntervalMs) {
            return;
        }
        last_watchdog_check_ms_ = now_ms;

        bool need_restart = false;
        if (settings_->app_.IsSteamUrl()) {
            // steam：任一已安装游戏 exe 进程在跑即视为存活；
            // 注入流程每轮按 exe 名自行发现新 pid，无需收养
            if (!steam_game_ || !steam_game_->Ready()) {
                return;
            }
            std::vector<std::string> split_path;
            StringUtil::Split(settings_->app_.game_path_, split_path, "/");
            if (split_path.empty()) {
                return;
            }
            auto steam_id = std::atoi(split_path.at(split_path.size()-1).c_str());
            auto installed_games = steam_game_->GetInstalledGames();
            auto it = std::find_if(installed_games.begin(), installed_games.end(), [steam_id](const SteamAppPtr& app) {
                return app->app_id_ == steam_id;
            });
            if (it == installed_games.end()) {
                return;
            }
            auto processes = ProcessHelper::GetProcessList(false);
            for (const auto& process : processes) {
                auto exe_name = FileUtil::GetFileNameFromPath(process->exe_full_path_);
                if (std::find((*it)->exe_names_.begin(), (*it)->exe_names_.end(), exe_name) != (*it)->exe_names_.end()) {
                    game_ever_seen_ = true;
                    return;
                }
            }
            // 冷启动宽限：拉起 url 后游戏进程可能几十秒才出现，期间不得重复拉
            need_restart = game_ever_seen_ &&
                (now_ms - last_game_restart_ms_.load() >= kSteamRelaunchGraceMs);
        } else if (!settings_->app_.game_view_path_.empty()) {
            // UE boot/view：view 进程在 → 存活；view 不在但 boot 还活着：
            // 首轮加载中（view 还没出现过）则等待；view 出现过说明这次是 view 崩了，
            // 残留的外壳一并杀掉再走重启，避免双外壳
            auto norm_view = StringUtil::ToLowerCpy(settings_->app_.game_view_path_);
            StringUtil::Replace(norm_view, "/", "\\");
            for (const auto& process : ProcessHelper::GetProcessList(false)) {
                auto norm_exe = StringUtil::ToLowerCpy(process->exe_full_path_);
                StringUtil::Replace(norm_exe, "/", "\\");
                if (norm_exe == norm_view) {
                    game_ever_seen_ = true;
                    view_ever_seen_ = true;
                    return;
                }
            }
            if (IsProcessAlive(target_pid_)) {
                if (!view_ever_seen_) {
                    return;
                }
                LOGW("UE view process gone but boot pid: {} still alive, killing lingering boot.", target_pid_.load());
                ProcessUtil::KillProcess(target_pid_);
            }
            need_restart = game_ever_seen_;
        } else {
            // 普通 app
            if (IsProcessAlive(target_pid_)) {
                game_ever_seen_ = true;
                return;
            }
            // 外部手动重启（同 exe 名新 pid）→ 收养，交给注入流程 re-hook，不重复启动
            if (!settings_->app_.game_path_.empty()) {
                const auto target_exe_name = FileUtil::GetFileNameFromPath(settings_->app_.game_path_);
                for (const auto& process : ProcessHelper::GetProcessList(false)) {
                    if (process->pid_ != target_pid_.load() &&
                        FileUtil::GetFileNameFromPath(process->exe_full_path_) == target_exe_name) {
                        LOGW("Game restarted externally, adopt new pid: {} (old: {}), will re-inject.",
                             process->pid_, target_pid_.load());
                        target_pid_ = process->pid_;
                        injected_ = false;
                        inject_alive_fail_count_ = 0;
                        ResetInjectRetryState();
                        waiting_first_frame_ = true;
                        NotifyGameStatus(tc::GameStatusChanged::kGameRestarting,
                                         "game restarted externally, re-hooking");
                        return;
                    }
                }
            }
            need_restart = game_ever_seen_;
        }

        if (!need_restart) {
            return;
        }
        if (now_ms - last_game_restart_ms_.load() < kGameRestartMinIntervalMs) {
            return;
        }
        last_game_restart_ms_ = now_ms;
        LOGW("Game process gone, restarting game: {}", settings_->app_.game_path_);
        waiting_first_frame_ = true;
        NotifyGameStatus(tc::GameStatusChanged::kGameRestarting, settings_->app_.game_path_);
        injected_ = false;
        inject_alive_fail_count_ = 0;
        ResetInjectRetryState();
        StartProcessWithHook();
    }

    bool AppManagerWinImpl::InjectCaptureDllForSteamApp() {
        std::vector<std::string> split_path;
        StringUtil::Split(settings_->app_.game_path_, split_path, "/");
        if (split_path.empty()) {
            return false;
        }
        auto steam_id = std::atoi(split_path.at(split_path.size()-1).c_str());
        if (steam_id <= 0) {
            return false;
        }

        if (!steam_game_->Ready()) {
            LOGW("Steam not ready.");
            steam_game_->RequestSteamGames();
            if (!steam_game_->Ready()) {
                return false;
            }
        }

        // find exe paths by steam app id
        auto installed_games = steam_game_->GetInstalledGames();
        auto it = std::find_if(installed_games.begin(), installed_games.end(), [steam_id](const SteamAppPtr& app) {
            return app->app_id_ == steam_id;
        });
        if (it == installed_games.end()) {
            return false;
        }
        SteamAppPtr target_app = *it;

        // find pids by exes
        std::vector<ProcessInfoPtr> processes_info;
        for (const std::string& exe_name : target_app->exe_names_) {
            auto processes = ProcessHelper::GetProcessList(false);
            for (auto& process : processes) {
                auto process_exe_name = FileUtil::GetFileNameFromPath(process->exe_full_path_);
                if (process_exe_name == exe_name) {
                    //LOGI("find target process exe: {}", exe_name);
                    auto ret = WinHelper::FindHwndByPid(process->pid_);
                    if (ret.ok_ && ret.value_) {
                        DWORD process_id = 0;
                        auto thread_id = GetWindowThreadProcessId(ret.value_, &process_id);
                        if (thread_id != 0 && process_id == process->pid_) {
                            process->thread_id_ = thread_id;
                        }
                        //LOGI("xxx PID:{} , TID:{}, origin PID:{}", process_id, thread_id, process.pid_);
                    }
                    processes_info.push_back(process);

                    //添加，关闭应用时直接退出所有应用
                    if (settings_->capture_.capture_video_type_ == Capture::kCaptureScreen) {
                        AddFoundPid(process);
                    }
                    break;
                }
            }
        }

        // inject it
        if (settings_->capture_.IsVideoInnerCapture()) {
            if (processes_info.empty()) {
                return false;
            }
            // 多候选进程（如 Steam launcher 与游戏本体同名）只注入一个：
            // 优先有可见主窗口的进程，其次最大 pid（通常最后创建）
            ProcessInfoPtr target_process_info = nullptr;
            for (const auto& process : processes_info) {
                if (process->thread_id_ > 0) {
                    target_process_info = process;
                    break;
                }
                if (!target_process_info || process->pid_ > target_process_info->pid_) {
                    target_process_info = process;
                }
            }

            auto result = WinHelper::IsDllInjected(target_process_info->pid_, kX86DllName, kX64DllName);
            auto process_exe_name = FileUtil::GetFileNameFromPath(target_process_info->exe_full_path_);
            if (result.ok_ && result.value_) {
                this->injected_ = true;
                target_pid_ = target_process_info->pid_;
                ResetInjectRetryState();
                return true;
            }

            //
            AddFoundPid(target_process_info);

            // Sync boot file BEFORE inject (SendAppMessage is async and races).
            if (rdApp) {
                rdApp->PrepareGameHookBoot(target_process_info->pid_);
            } else {
                LOGE("rdApp null, cannot write hook boot config before inject");
            }
            context_->SendAppMessage(MsgBeforeInject{
                .steam_app_ = target_app,
                .pid_ = target_process_info->pid_,
            });

            bool injected = InjectDll(target_process_info->pid_, target_process_info->thread_id_,
                                      target_process_info->is_x86_, kX86DllName, kX64DllName);
            // DllMain used to block >4s under loader lock; inject helper then
            // timed out even when the module was actually mapped. Treat mapped DLL as OK.
            if (!injected) {
                auto again = WinHelper::IsDllInjected(target_process_info->pid_, kX86DllName, kX64DllName);
                if (again.ok_ && again.value_) {
                    LOGW("Injector timed out/failed but tc_graphics.dll is mapped — treat as success");
                    injected = true;
                }
            }
            if (injected) {
                LOGI("Inject success for pid: {}, exe: {}", target_process_info->pid_, process_exe_name);
                this->injected_ = true;
                target_pid_ = target_process_info->pid_;
                ResetInjectRetryState();
                MsgObsInjected msg_injected;
                msg_injected.steam_app_ = target_app;
                msg_injected.pid_ = target_process_info->pid_;
                context_->SendAppMessage(msg_injected);
            } else {
                LOGE("Inject capture dll failed for pid: {}, is x86:{}, exe: {}",
                     target_process_info->pid_, target_process_info->is_x86_, process_exe_name);
            }
        }
        return true;
    }

    bool AppManagerWinImpl::InjectCaptureDllForNormalApp() {
        if (target_pid_ <= 0) {
            return false;
        }

        auto processes = ProcessHelper::GetProcessList(false);
        ProcessInfoPtr target_process_info = nullptr;

        // UE boot/view：外壳（target_pid_）只是启动器，注入目标是 view 真游戏
        // 进程——按 service 解析下发的完整路径精确匹配。view 尚未出现时返回
        // false（不算注入失败，worker 走低频等待，外壳初始化完成后下一轮命中）。
        const std::string& view_path = settings_->app_.game_view_path_;
        if (!view_path.empty()) {
            auto norm_view = StringUtil::ToLowerCpy(view_path);
            StringUtil::Replace(norm_view, "/", "\\");
            for (const auto& process : processes) {
                auto norm_exe = StringUtil::ToLowerCpy(process->exe_full_path_);
                StringUtil::Replace(norm_exe, "/", "\\");
                if (norm_exe == norm_view) {
                    target_process_info = process;
                    break;
                }
            }
            if (!target_process_info || !target_process_info->Valid()) {
                return false;
            }
            LOGI("UE view process found: pid={}, exe={}",
                 target_process_info->pid_, target_process_info->exe_full_path_);
        } else {
        for (const auto& process : processes) {
            if (process->pid_ == target_pid_) {
                target_process_info = process;
                auto process_exe_name = FileUtil::GetFileNameFromPath(process->exe_full_path_);
                //LOGI("Find the pid: {}, exe : {}", process->pid_, process_exe_name);
                break;
            }
        }
        if (!target_process_info) {
            LOGI("Can't find process info now: {}", target_pid_.load());
            return false;
        }

        if (!target_process_info->Valid()) {
            auto target_exe_name = FileUtil::GetFileNameFromPath(settings_->app_.game_path_);
            LOGE("Can't find app to inject, pid: {}, search for by exe: {}", target_pid_.load(), target_exe_name);
            uint32_t pid_by_exe = 0;
            for (const auto& process : processes) {
                auto process_exe_name = FileUtil::GetFileNameFromPath(process->exe_full_path_);
                if (process_exe_name == target_exe_name) {
                    LOGI("find target process exe: {}, pid: {}", target_exe_name, pid_by_exe);
                    pid_by_exe = process->pid_;
                    break;
                }
            }
            if (pid_by_exe == 0) {
                LOGE("find by exe failed, return.");
                return false;
            }
            target_pid_ = pid_by_exe;
        }
        }

        if (settings_->capture_.capture_video_type_ == Capture::kCaptureScreen) {
            AddFoundPid(target_process_info);
        }
        if (settings_->capture_.IsVideoInnerCapture()){
            if (this->injected_) {
                return true;
            }
            auto result = WinHelper::IsDllInjected(target_process_info->pid_, kX86DllName, kX64DllName);
            auto process_exe_name = FileUtil::GetFileNameFromPath(target_process_info->exe_full_path_);
            if (result.ok_ && result.value_) {
                LOGI("Pid: {} for: {} is already injected....", target_process_info->pid_, process_exe_name);
                this->injected_ = true;
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

            bool injected = InjectDll(target_process_info->pid_, target_process_info->thread_id_,
                                      target_process_info->is_x86_, kX86DllName, kX64DllName);
            // DllMain used to block >4s under loader lock; inject helper then
            // timed out even when the module was actually mapped. Treat mapped DLL as OK.
            if (!injected) {
                auto again = WinHelper::IsDllInjected(target_process_info->pid_, kX86DllName, kX64DllName);
                if (again.ok_ && again.value_) {
                    LOGW("Injector timed out/failed but tc_graphics.dll is mapped — treat as success");
                    injected = true;
                }
            }
            this->injected_ = injected;
            if (injected) {
                LOGI("Inject success for pid: {}, exe: {}", target_process_info->pid_, process_exe_name);
                target_pid_ = target_process_info->pid_;
                ResetInjectRetryState();
                MsgObsInjected msg_injected;
                SteamAppPtr mock_app = SteamApp::Make();
                mock_app->exes_.push_back(settings_->app_.game_path_);
                msg_injected.steam_app_ = mock_app;
                msg_injected.pid_ = target_process_info->pid_;
                context_->SendAppMessage(msg_injected);
            } else {
                LOGE("Inject capture dll failed for pid: {}, is x86:{}, exe: {}", target_process_info->pid_,
                     target_process_info->is_x86_, process_exe_name);
            }
        }
        return true;
    }

    bool AppManagerWinImpl::InjectDll(uint32_t pid, uint32_t tid, bool is_x86, const std::string& x86_dll, const std::string& x64_dll) {
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

        // 32 位目标明确拒绝：暂无 32 位 tc_graphics.dll，且 inject-library 会把
        // 64 位 LoadLibraryW 地址写进 WoW64 进程（行为未定义，可能崩游戏）。
        // 记 permanent failure，停止重试
        if (is_x86) {
            inject_gave_up_ = true;
            LOGE("暂不支持 32 位游戏, pid: {}, 注入已放弃（需要 32 位版 tc_graphics.dll）", pid);
            return false;
        }
        {
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (process) {
                BOOL wow64 = FALSE;
                bool target_is_x86 = IsWow64Process(process, &wow64) && wow64;
                CloseHandle(process);
                if (target_is_x86) {
                    inject_gave_up_ = true;
                    LOGE("暂不支持 32 位游戏, pid: {}, 注入已放弃（需要 32 位版 tc_graphics.dll）", pid);
                    return false;
                }
            }
        }

        // 权限/完整性检查：目标以管理员运行而 Render 为普通权限时，injector 内
        // open_process(PROCESS_ALL_ACCESS) 永远失败，这里提前探测给出明确报错。
        // 此类失败与其它失败一样持续重试（用户可能随后以管理员重启 Render）
        {
            HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
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
            CloseHandle(process);
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
        CloseCurrentApp();
    }

    void* AppManagerWinImpl::GetWindowHandle() {
        return target_window_info_.win_handle;
    }

    void AppManagerWinImpl::CloseCurrentApp() {
        for (const auto& pi : found_process_info_) {
            LOGI("Will kill target pid: {}, exe: {}", pi->pid_, pi->exe_full_path_);
            ProcessUtil::KillProcess(pi->pid_);
        }
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
            auto handle = GetParent(info.win_handle);
            while (handle) {
                info.win_handle = handle;
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
