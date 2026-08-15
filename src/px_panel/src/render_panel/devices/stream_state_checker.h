//
// Created by RGAA on 24/05/2025.
//

#ifndef GAMMARAY_STREAM_STATE_CHECKER_H
#define GAMMARAY_STREAM_STATE_CHECKER_H

#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace px_cms
{
    class CmsStream;
}

namespace px
{

    class GrContext;
    class GrSettings;
    class MessageListener;

    using OnStreamStateCheckedCallback = std::function<void(std::vector<std::shared_ptr<px_cms::CmsStream>>)>;

    class StreamStateChecker : public std::enable_shared_from_this<StreamStateChecker> {
    public:
        explicit StreamStateChecker(const std::shared_ptr<GrContext>& ctx);
        void Start();
        void Exit();
        void SetOnCheckedCallback(OnStreamStateCheckedCallback&&);
        void UpdateCurrentStreamItems(const std::vector<std::shared_ptr<px_cms::CmsStream>>& items);
    private:
        void CheckState(const std::vector<std::shared_ptr<px_cms::CmsStream>>& items);

    private:
        GrSettings* settings_ = nullptr;
        std::shared_ptr<GrContext> context_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        OnStreamStateCheckedCallback on_checked_cbk_;
    };

}

#endif //GAMMARAY_STREAM_STATE_CHECKER_H
