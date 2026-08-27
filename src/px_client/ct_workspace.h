//
// Created by RGAA on 2023-12-27.
//

#ifndef TC_CLIENT_PC_WORKSPACE_H
#define TC_CLIENT_PC_WORKSPACE_H

#include <QWidget>
#include <QMainWindow>
#include <QLibrary>
#include <map>
#include <memory>
#include <vector>
#include <qlist.h>
#include "thunder_sdk.h"
#include "theme/QtAdvancedStylesheet.h"
#include "px_client/ct_app_message.h"
#include "ct_base_workspace.h"
#include "latest_frame_dispatch_queue.h"

namespace px
{

    class PxRenderView;

    class Workspace : public BaseWorkspace {
    public:
        static std::shared_ptr<Workspace> Make(const std::shared_ptr<ClientContext>& ctx, const std::shared_ptr<ThunderSdkParams>& params, QWidget* parent = nullptr);

        ~Workspace() override;
        bool eventFilter(QObject* watched, QEvent* event) override;
        void SendWindowsKey(unsigned long vk, bool down) override;
    protected:
        explicit Workspace(const std::shared_ptr<ClientContext>& ctx, const std::shared_ptr<ThunderSdkParams>& params, QWidget* parent = nullptr);
        void InitRenderViews(const std::shared_ptr<ThunderSdkParams>& params) override;
        void RegisterBaseListeners() override;
    private:
        struct PendingDecodedVideoFrame {
            std::shared_ptr<RawImage> image_;
            SdkCaptureMonitorInfo info_;
        };

        void Init() override;
        void RegisterSdkMsgCallbacks() override;
        void RenderDecodedVideoFrame(const std::shared_ptr<RawImage>& image,
                                     const SdkCaptureMonitorInfo& info);
        void CalculateAspectRatio() override;
        void SwitchToFillWindow() override;
        void UpdateRenderViewsStatus(bool force_layout_screens) override;
        void OnGetCaptureMonitorsCount(int monitors_count) override;
        void OnGetCaptureMonitorName(std::string monitor_name) override;
        void EnsureRenderViewCount(int requested_count);
        void PositionRenderViews();
    private:
        std::vector<std::shared_ptr<PxRenderView>> render_views_;
        std::shared_ptr<LatestFrameDispatchQueue<int, PendingDecodedVideoFrame>> video_frame_dispatch_queue_
            = std::make_shared<LatestFrameDispatchQueue<int, PendingDecodedVideoFrame>>();
  
        EMultiMonDisplayMode multi_display_mode_ = EMultiMonDisplayMode::kTab;     
    private:
        void ListenMultiMonDisplayModeMessage();

        // auto split the windows
        std::once_flag send_split_windows_flag_;

        // auto layout the windows
        std::once_flag layout_windows_;
    };

}

#endif //TC_CLIENT_PC_WORKSPACE_H
