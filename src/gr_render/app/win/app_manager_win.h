//
// Created by RGAA on 2023-12-21.
//

#ifndef TC_APPLICATION_APP_MANAGER_WIN_H
#define TC_APPLICATION_APP_MANAGER_WIN_H

#include <map>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "app/app_manager.h"
#include "tc_common_new/win32/process_helper.h"

namespace tc
{

    class RdSettings;
    class SteamGame;

    class AppManagerWinImpl : public AppManager {
    public:

        explicit AppManagerWinImpl(const std::shared_ptr<RdContext>& ctx);
        ~AppManagerWinImpl() override;

        void Init() override;
        bool StartProcessWithHook() override;
        bool StartProcess() override;
        void Exit() override;
        void* GetWindowHandle() override;
        void CloseCurrentApp() override;

    private:
        void InjectCaptureDllIfNeeded();
        void InjectWorkerLoop();
        // 返回值：本轮是否真正执行了一次注入尝试（成功或失败）；
        // false 表示目标进程尚未出现等"还不到注入时机"的情况，不计入失败次数
        bool InjectCaptureDllForSteamApp();
        bool InjectCaptureDllForNormalApp();
        void VerifyInjectedStillAlive();
        void ResetInjectRetryState();
        // gave_up 状态下的低频探测：目标进程消失或同 exe 出现新 pid（游戏重启）
        // 则清除 gave_up 并恢复注入流程
        void ProbeGaveUpTargetGone();
        bool InjectDll(uint32_t pid, uint32_t tid, bool is_x86, const std::string& x86_dll, const std::string& x64_dll);
        void AddFoundPid(const ProcessInfoPtr& target_pi);
        static WindowInfos SearchWindowByPid(uint32_t pid);
        static WindowInfo GetTargetWindowInfo(const WindowInfos& infos);

    private:
        RdSettings* settings_;
        std::atomic_ulong target_pid_ = 0;
        WindowInfo target_window_info_;
        std::atomic<bool> injected_ = false;
        // 找到的所有的属于这个应用的pid
        std::vector<ProcessInfoPtr> found_process_info_;
        std::shared_ptr<SteamGame> steam_game_ = nullptr;

        // 注入在独立 worker 线程上执行，MsgTimer 只负责"踢"一下，不在消息线程上阻塞等待 injector
        std::shared_ptr<std::thread> inject_worker_ = nullptr;
        std::mutex inject_mtx_;
        std::condition_variable inject_cv_;
        std::atomic<bool> inject_worker_exit_ = false;
        std::atomic<bool> inject_requested_ = false;
        std::atomic<bool> inject_gave_up_ = false;
        int inject_attempts_ = 0;
        int inject_alive_fail_count_ = 0;
        // 最近一次实际尝试注入的目标 pid（含失败/32 位拒绝），gave_up 探测用它
        // 判断游戏是否已重启（steam 场景 target_pid_ 在注入成功前一直是 0）
        std::atomic<uint32_t> last_inject_target_pid_ = 0;

    };

}


#endif //TC_APPLICATION_APP_MANAGER_WIN_H
