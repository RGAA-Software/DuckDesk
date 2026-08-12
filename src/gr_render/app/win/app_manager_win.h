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
#include "tc_message.pb.h"

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
        // 游戏看门狗（仅 game-hook 模式）：游戏进程死了则重新拉起（5s 节流），
        // 被外部手动重启（同 exe 新 pid）则收养新 pid 交给注入流程 re-hook。
        // 仅在游戏成功跑起来过一次（game_ever_seen_）之后才介入，避免与首轮启动竞争。
        void EnsureGameRunning();
        // StartProcessWithHook 成功拉起游戏后调用：标记"游戏拉起过"，并刷新重启节流计时
        void MarkGameLaunched();
        // 拉起游戏进程：优先以控制台会话登录用户身份（SYSTEM 直接拉会落在
        // SYSTEM profile，游戏网络/用户配置不对），拿不到 token 回退普通 CreateProcess
        uint32_t LaunchGameProcess(const std::string& u8_exec, const std::vector<std::string>& args);
        // 把游戏进程挂进 game_job_(KILL_ON_JOB_CLOSE):render 无论被停止还是强杀,
        // 句柄关闭时 OS 自动杀掉整棵游戏进程树,不再残留注入过的游戏
        void AssignGameToJob(uint32_t pid);
        // 游戏状态变化（死亡重启/恢复）广播给已连接客户端
        void NotifyGameStatus(tc::GameStatusChanged::GameStatus status, const std::string& detail);
        // 进程是否存活（OpenProcess + STILL_ACTIVE）
        static bool IsProcessAlive(uint32_t pid);
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
        // game-hook 模式下持有:把拉起/收养的游戏进程都挂进来,render 进程退出
        // (包括被 taskkill)时由 OS 连带杀掉游戏进程树
        HANDLE game_job_ = nullptr;

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

        // 游戏看门狗状态：游戏拉起/存活过一次之后才允许自动重启（避免首轮启动阶段误介入）
        std::atomic<bool> game_ever_seen_ = false;
        // UE view 进程出现过才置位：区分"首轮加载中"与"view 崩溃后外壳残留"
        std::atomic<bool> view_ever_seen_ = false;
        // 看门狗重启（或收养外部重启）后置位；重启后第一帧到达才向客户端发"已恢复"——
        // 注入成功时游戏往往还在冷启动（注入仅需几十 ms，出画面要几十秒）
        std::atomic<bool> waiting_first_frame_ = false;
        std::atomic<int64_t> last_game_restart_ms_ = 0;
        std::atomic<int64_t> last_watchdog_check_ms_ = 0;

    };

}


#endif //TC_APPLICATION_APP_MANAGER_WIN_H
