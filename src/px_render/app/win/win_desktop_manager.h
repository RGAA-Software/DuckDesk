#pragma once
#include <memory>

namespace px
{

    class PxContext;
    class WinMessageLoop;

    class WinDesktopManager {
    public:
        static std::shared_ptr<WinDesktopManager> Make(const std::shared_ptr<PxContext>& ctx);
        explicit WinDesktopManager(const std::shared_ptr<PxContext>& ctx);
        ~WinDesktopManager();
        // 刷新桌面
        void UpdateDesktop();
    public:
    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<WinMessageLoop> message_loop_;
    };

}