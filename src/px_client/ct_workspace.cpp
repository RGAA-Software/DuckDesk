//
// Created by RGAA on 2023-12-27.
//

#include <QHBoxLayout>
#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <QThread>
#include <dwmapi.h>
#include "px_client/ct_workspace.h"
#include "thunder_sdk.h"
#include "px_client/ct_client_context.h"
#include "px_common/data.h"
#include "px_common/log.h"
#include "px_common/message_notifier.h"
#include "px_client/ct_audio_player.h"
#include "ui/float_controller.h"
#include "ui/float_controller_panel.h"
#include "px_client/ct_app_message.h"
#include "px_client/ct_settings.h"
#include "ui/float_notification_handle.h"
#include "ui/notification_panel.h"
#include "px_qt_widget/sized_msg_box.h"
#include "ui/ct_statistics_panel.h"
#include "ui/no_margin_layout.h"
#include "px_client_sdk/sdk_messages.h"
#include "px_common/process_util.h"
#include "ui/float_button_state_indicator.h"
#include "ct_main_progress.h"
#include "px_qt_widget/widgetframe/mainwindow_wrapper.h"
#include "px_dialog.h"
#include "px_render_view.h"
#include "render_view_capacity.h"
#include "ct_const_def.h"
#include "px_common/file.h"
#include "network/ct_panel_client.h"
#include "px_common/time_util.h"
#include "px_qt_widget/notify/notifymanager.h"
#include "front_render/opengl/ct_opengl_video_widget.h"
#include "front_render/vulkan/pl_vulkan.h"

namespace px
{
    std::shared_ptr<Workspace> Workspace::Make(const std::shared_ptr<ClientContext>& ctx, const std::shared_ptr<ThunderSdkParams>& params, QWidget* parent) {
        struct WorkspaceEnabler final : Workspace {
            WorkspaceEnabler(const std::shared_ptr<ClientContext>& app_ctx,
                             const std::shared_ptr<ThunderSdkParams>& app_params,
                             QWidget* app_parent)
                : Workspace(app_ctx, app_params, app_parent) {}
        };

        auto workspace = std::make_shared<WorkspaceEnabler>(ctx, params, parent);
        workspace->Init();
        return workspace;
    }

    Workspace::Workspace(const std::shared_ptr<ClientContext>& ctx, const std::shared_ptr<ThunderSdkParams>& params, QWidget* parent) : BaseWorkspace(ctx, params, parent) {
        this->context_->full_functionality_ = true;
    }

    Workspace::~Workspace() {
        video_frame_dispatch_queue_->Stop();
        render_views_.clear();
    }

    void Workspace::RegisterBaseListeners() {
        BaseWorkspace::RegisterBaseListeners();
        ListenMultiMonDisplayModeMessage();
    }

    void Workspace::ListenMultiMonDisplayModeMessage() {
        const std::weak_ptr<Workspace> weak_workspace =
            std::static_pointer_cast<Workspace>(shared_from_this());
        msg_listener_->Listen<MsgClientMultiMonDisplayMode>(
            [weak_workspace](const MsgClientMultiMonDisplayMode& msg) {
            const auto workspace = weak_workspace.lock();
            if (!workspace) {
                return;
            }
            workspace->context_->PostUITask([weak_workspace, msg]() {
                const auto task_workspace = weak_workspace.lock();
                if (!task_workspace) {
                    return;
                }
                task_workspace->multi_display_mode_ = msg.mode_;
                if (EMultiMonDisplayMode::kSeparate == task_workspace->multi_display_mode_) {
                    if (task_workspace->monitors_count_ > 1) {
                        task_workspace->setWindowTitle(
                            task_workspace->origin_title_name_ +
                            QStringLiteral(" (Desktop:%1)").arg(QString::number(1)));
                    }
                    else {
                        task_workspace->setWindowTitle(task_workspace->origin_title_name_);
                    }

                    for (const auto& index_name : task_workspace->monitor_index_map_name_) {
                        if (task_workspace->render_views_.size() > index_name.first) {
                            if (task_workspace->render_views_[index_name.first]) {
                                task_workspace->render_views_[index_name.first]->SetMonitorName(
                                    index_name.second);
                            }
                        }
                    }
                }
                else if (EMultiMonDisplayMode::kTab == task_workspace->multi_display_mode_) {
                    task_workspace->setWindowTitle(task_workspace->origin_title_name_);
                    if (task_workspace->monitor_index_map_name_.count(msg.current_cap_mon_index_)) {
                        task_workspace->render_views_[kMainRenderViewIndex]->SetMonitorName(
                            task_workspace->monitor_index_map_name_[msg.current_cap_mon_index_]);
                    }
                }
                task_workspace->SendUpdateDesktopMessage();
            });
        });
    }

    void Workspace::Init() {
        BaseWorkspace::Init();
    }

    void Workspace::RegisterSdkMsgCallbacks() {
        BaseWorkspace::RegisterSdkMsgCallbacks();

        auto weak_workspace = weak_from_this();
        std::weak_ptr<ClientContext> weak_context = context_;
        auto dispatch_queue = video_frame_dispatch_queue_;
        sdk_->SetOnVideoFrameDecodedCallback(
            [weak_workspace, weak_context, dispatch_queue](std::shared_ptr<RawImage> image,
                                                           const SdkCaptureMonitorInfo& info) {
                if (!image) {
                    return;
                }

                if (!dispatch_queue->Push(info.mon_index_, PendingDecodedVideoFrame{
                        .image_ = std::move(image),
                        .info_ = info,
                    })) {
                    return;
                }

                auto context = weak_context.lock();
                if (!context) {
                    dispatch_queue->Stop();
                    return;
                }

                context->PostUITask([weak_workspace, dispatch_queue]() {
                    auto workspace = std::dynamic_pointer_cast<Workspace>(weak_workspace.lock());
                    if (!workspace) {
                        dispatch_queue->Stop();
                        return;
                    }

                    for (auto& frame : dispatch_queue->TakePending()) {
                        workspace->RenderDecodedVideoFrame(frame.image_, frame.info_);
                    }
                });
            });
    }

    void Workspace::RenderDecodedVideoFrame(const std::shared_ptr<RawImage>& image,
                                            const SdkCaptureMonitorInfo& info) {
        Q_ASSERT(QThread::currentThread() == thread());
        if (!image || remote_force_closed_) {
            return;
        }
        if (!has_frame_arrived_) {
            has_frame_arrived_ = true;
            LOGI("First decoded video frame reached UI renderer: monitor={}, {}x{}",
                 info.mon_name_, image->img_width, image->img_height);
            UpdateVideoWidgetSize();
        }
        //LOGI("SdkCaptureMonitorInfo mon_index_: {}, w: {}, h: {}", info.mon_index_, image->img_width, image->img_height);
        if (EMultiMonDisplayMode::kTab == multi_display_mode_) {
            if (!render_views_.empty()) {
                if (render_views_[kMainRenderViewIndex]) {
                    render_views_[kMainRenderViewIndex]->RefreshCapturedMonitorInfo(info);
                    // WebRTC local frames arrive as I420 (no AVFrame); they must use RefreshImage.
                    if (this->params_->support_vulkan_ && image->vulkan_av_frame_) {
                        const auto obj = reinterpret_cast<uintptr_t>(
                            render_views_[kMainRenderViewIndex].get());
                        pl_vulkan_->RenderFrame(obj, image->vulkan_av_frame_);
                        render_views_[kMainRenderViewIndex]->UpdateFullColorState(image->full_color_);
                    }
                    else {
                        render_views_[kMainRenderViewIndex]->RefreshImage(image);
                    }
                }
            }
        }
        else if (EMultiMonDisplayMode::kSeparate == multi_display_mode_) {
            if (info.mon_index_ >= 0) {
                EnsureRenderViewCount(info.mon_index_ + 1);
            }
            if (info.mon_index_ >= 0
                && render_views_.size() > static_cast<std::size_t>(info.mon_index_)) {
                if (render_views_[info.mon_index_]) {
                    render_views_[info.mon_index_]->RefreshCapturedMonitorInfo(info);
                    // WebRTC local frames arrive as I420 (no AVFrame); they must use RefreshImage.
                    if (this->params_->support_vulkan_ && image->vulkan_av_frame_) {
                        pl_vulkan_->RenderFrame(
                            reinterpret_cast<uintptr_t>(render_views_[info.mon_index_].get()),
                            image->vulkan_av_frame_);
                    }
                    else {
                        render_views_[info.mon_index_]->RefreshImage(image);
                    }
                    if (!render_views_[info.mon_index_]->GetActiveStatus()) {
                        render_views_[info.mon_index_]->SetActiveStatus(true);
                        UpdateRenderViewsStatus(false);
                    }
                }
            }
        }
        context_->UpdateCapturingMonitorInfo(info);
    }

    void Workspace::SendWindowsKey(unsigned long vk, bool down) {
        if (!render_views_.empty() && !remote_force_closed_) {
            if (render_views_[kMainRenderViewIndex]) {
                render_views_[kMainRenderViewIndex]->SendKeyEvent(vk, down);
            }
        }
    }

    void Workspace::CalculateAspectRatio() {
        for (auto render_view : render_views_) {
            if (render_view && !render_view->isHidden()) {
                render_view->CalculateAspectRatio();
            }
        }
    }

    void Workspace::SwitchToFillWindow() {
        for (auto render_view : render_views_) {
            if (render_view && !render_view->isHidden()) {
                render_view->SwitchToFillWindow();
            }
        }
    }

    void Workspace::UpdateRenderViewsStatus(bool force_layout_screens) {
        QList<QScreen*> screens = QGuiApplication::screens();
        if (EMultiMonDisplayMode::kTab ==  multi_display_mode_) {
            for (auto render_view : render_views_) {
                if (render_view->IsMainView()) {
                    if (full_screen_) {
                        if (!force_layout_screens) {
                            WidgetSelectMonitor(this, screens);
                        }
                        this->showFullScreen();
                        render_view->showFullScreen();
                        //px::QWidgetHelper::SetBorderInFullScreen(this, true);
                        //px::QWidgetHelper::SetBorderInFullScreen(render_view, true);
                    }
                    else {
                        if (this->isMaximized()) {
                            this->showMaximized();
                            render_view->showMaximized();
                        }
                        else {
                            this->showNormal();
                            render_view->showNormal();
                        }
                    }
                }
                else {
                    render_view->hide();
                }
            }
        }
        else if (EMultiMonDisplayMode::kSeparate == multi_display_mode_) {
            for (auto render_view : render_views_) {
                if (render_view->GetActiveStatus()) {
                    if (full_screen_) {
                        if (render_view->IsMainView()) {
                            if (!force_layout_screens) {
                                WidgetSelectMonitor(this, screens);
                            }
                            this->showFullScreen();
                            this->raise();
                            render_view->showFullScreen();
                            //px::QWidgetHelper::SetBorderInFullScreen(this, true);
                        }
                        else {
                            if (!force_layout_screens) {
                                WidgetSelectMonitor(render_view.get(), screens);
                            }
                            render_view->showFullScreen();
                        }
                        //px::QWidgetHelper::SetBorderInFullScreen(render_view, true);
                    }
                    else {
                        if (render_view->isMaximized()) {
                            render_view->showMaximized();
                            if (render_view->IsMainView()) {
                                this->showMaximized();
                            }
                        }
                        else {
                            render_view->showNormal();
                            if (render_view->IsMainView()) {
                                this->showNormal();
                            }
                        }
                    }
                }
                else {
                    render_view->hide();
                }
            }
        }
    }

    void Workspace::OnGetCaptureMonitorsCount(int monitors_count) {
        monitors_count_ = monitors_count;
        EnsureRenderViewCount(monitors_count);
        if (monitors_count <= 1) {
            setWindowTitle(origin_title_name_);
        }
        const int real_display_monitors = std::clamp(
            monitors_count, 0, static_cast<int>(render_views_.size()));
        for (int index = 0; index < real_display_monitors; ++index) {
            render_views_[index]->SetActiveStatus(true);
        }

        for (std::size_t index = static_cast<std::size_t>(real_display_monitors);
             index < render_views_.size(); ++index) {
            render_views_[index]->SetActiveStatus(false);
        }
        UpdateRenderViewsStatus(false);

        if (monitors_count_ > 1) {
            const auto workspace = std::static_pointer_cast<Workspace>(shared_from_this());
            std::call_once(send_split_windows_flag_, [workspace]() {
                if (workspace->settings_->split_windows_) {
                    workspace->SendSwitchMonitorMessage(kCaptureAllMonitorsSign);
                    LOGI("SendSwitchMonitorMessage(kCaptureAllMonitorsSign)");
                }
            });
        }

        const std::weak_ptr<Workspace> weak_workspace =
            std::static_pointer_cast<Workspace>(shared_from_this());
        std::call_once(layout_windows_, [weak_workspace, monitors_count]() {
            const auto workspace = weak_workspace.lock();
            if (!workspace) {
                return;
            }
            if (monitors_count != 2) {
                workspace->context_->PostDelayUITask([weak_workspace]() {
                    if (const auto task_workspace = weak_workspace.lock();
                        task_workspace && task_workspace->settings_->auto_layout_screens_) {
                        task_workspace->showMaximized();
                    }
                }, 100);
            }
            else {
                workspace->context_->PostDelayUITask([weak_workspace, monitors_count]() {
                    const auto task_workspace = weak_workspace.lock();
                    if (task_workspace && task_workspace->settings_->auto_layout_screens_) {
                        // layout it
                        const auto screens = qApp->screens();
                        if (monitors_count == 2 && screens.size() == monitors_count) {

                            task_workspace->showNormal();

                            std::map<int, QScreen*> scs;
                            for (const auto& sc : screens) {
                                auto left = sc->geometry().left();
                                scs.insert({left, sc});
                                LOGI("===> Geometry: {},{}, {}, {}", sc->geometry().left(), sc->geometry().top(), sc->geometry().width(), sc->geometry().height());
                            }

                            if (task_workspace->render_views_.size() >= scs.size()) {
                                int gv_index = 0;
                                for (const auto& sc: scs | std::views::values) {
                                    if (gv_index == 0) {
                                        LOGI("===> 0 Geometry: {},{}, {}, {}", sc->geometry().left(), sc->geometry().top(), sc->geometry().width(), sc->geometry().height());
                                        auto ml = sc->geometry().left() + (sc->geometry().width() - task_workspace->width())/2;
                                        auto mt = (sc->geometry().height() - task_workspace->height())/2;
                                        task_workspace->move(ml, mt);
                                        task_workspace->windowHandle()->setScreen(sc);
                                    }
                                    else {
                                        const auto view = task_workspace->render_views_[gv_index];
                                        auto ml = sc->geometry().left() + (sc->geometry().width() - task_workspace->width())/2;
                                        auto mt = (sc->geometry().height() - task_workspace->height())/2;
                                        view->move(ml, mt);
                                        if (const auto win = view->windowHandle()) {
                                            LOGI("===> 1 Geometry: {},{}, {}, {}", sc->geometry().left(), sc->geometry().top(), sc->geometry().width(), sc->geometry().height());
                                            win->setScreen(sc);
                                        }
                                    }
                                    ++gv_index;
                                }
                            }

                            task_workspace->context_->PostDelayUITask([weak_workspace]() {
                                if (const auto layout_workspace = weak_workspace.lock()) {
                                    layout_workspace->full_screen_ = true;
                                    layout_workspace->UpdateRenderViewsStatus(true);
                                }
                            }, 50);
                        }
                    }
                }, 100);
            }
        });
    }

    void Workspace::OnGetCaptureMonitorName(std::string monitor_name) {
        LOGI("OnGetCaptureMonitorName monitor_name: {}", monitor_name);
        for (const auto& index_name : monitor_index_map_name_) {
            if (render_views_.size() > index_name.first) {
                if (render_views_[index_name.first]) {
                    render_views_[index_name.first]->SetMonitorName(index_name.second);
                }
            }
        }

        if (kCaptureAllMonitorsSign == monitor_name) {
            multi_display_mode_ = EMultiMonDisplayMode::kSeparate;
            if (monitors_count_ > 1) {
                setWindowTitle(origin_title_name_ + QStringLiteral(" (Desktop:%1)").arg(QString::number(1)));
            } 
        }
        else {
            multi_display_mode_ = EMultiMonDisplayMode::kTab;
            if (render_views_[kMainRenderViewIndex]) {
                render_views_[kMainRenderViewIndex]->SetMonitorName(monitor_name);
            }
        }
    }

    void Workspace::InitRenderViews(const std::shared_ptr<ThunderSdkParams>& params) {
        this->resize(def_window_size_);
        EnsureRenderViewCount(1);
        params->render_type_name_ = render_type_name_ =
            render_views_[kMainRenderViewIndex]->GetRenderTypeName();

        const std::weak_ptr<Workspace> weak_workspace =
            std::static_pointer_cast<Workspace>(shared_from_this());
        QTimer::singleShot(1, this, [weak_workspace]() {
            const auto workspace = weak_workspace.lock();
            if (!workspace) {
                return;
            }
            workspace->PositionRenderViews();
        });
    }

    void Workspace::EnsureRenderViewCount(int requested_count) {
        Q_ASSERT(QThread::currentThread() == thread());
        const auto configured_limit =
            NormalizeRenderViewLimit(settings_->max_number_of_screen_window_);
        const auto target_count = ResolveRequiredRenderViewCount(
            requested_count,
            settings_->max_number_of_screen_window_);
        if (requested_count > static_cast<int>(configured_limit)) {
            LOGW("Remote reported {} monitors, capped by configured render view limit {}",
                 requested_count, configured_limit);
        }
        if (render_views_.size() >= target_count) {
            return;
        }

        const bool support_vulkan = params_->support_vulkan_;
        render_views_.reserve(target_count);
        while (render_views_.size() < target_count) {
            const auto index = render_views_.size();
            std::shared_ptr<PxRenderView> render_view;
            if (index == kMainRenderViewIndex) {
                render_view = std::make_shared<PxRenderView>(context_, sdk_, params_, this);
                render_view->resize(def_window_size_);
                render_view->show();
                render_view->SetMainView(true);
                setCentralWidget(render_view.get());
                if (support_vulkan) {
                    const auto hwnd = render_view->GetVideoHwnd();
                    const auto obj = reinterpret_cast<uintptr_t>(render_view.get());
                    const bool res = pl_vulkan_->Initialize(obj, hwnd);
                    if (!res) {
                        LOGE("pl_vulkan_->Initialize failed.");
                    }
                }
            }
            else {
                render_view = std::make_shared<PxRenderView>(context_, sdk_, params_, nullptr);
                render_view->InitOverlayWidget();
                render_view->resize(def_window_size_);
                render_view->hide();
                render_view->SetMainView(false);
                render_view->installEventFilter(this);
                render_view->setWindowTitle(
                    origin_title_name_
                    + QStringLiteral(" (Desktop:%1)").arg(QString::number(index + 1)));
       
                const auto obj = reinterpret_cast<uintptr_t>(render_view.get());
                if (support_vulkan) {
                    const auto hwnd = render_view->GetVideoHwnd();
                    const bool res = pl_vulkan_->CreateRenderComponent(obj, hwnd);
                    if (!res) {
                        LOGE("pl_vulkan_->CreateRenderComponent failed.");
                    }
                }
            }
            render_views_.push_back(std::move(render_view));
        }
        PositionRenderViews();
        LOGI("Render view pool expanded on demand: requested={}, active_capacity={}, configured_limit={}",
             requested_count, render_views_.size(), configured_limit);
    }

    void Workspace::PositionRenderViews() {
        Q_ASSERT(QThread::currentThread() == thread());
        if (const auto primary_screen = QGuiApplication::primaryScreen()) {
            const QRect screen_geometry = primary_screen->geometry();
            const int x = (screen_geometry.width() - width()) / 2;
            const int y = (screen_geometry.height() - height()) / 2;
            if (!isVisible()) {
                move(x, y);
            }
        }

        const QPoint workspace_position = pos();
        constexpr int kXOffset = 80;
        constexpr int kYOffset = 40;
        for (std::size_t index = 1; index < render_views_.size(); ++index) {
            const auto& render_view = render_views_[index];
            if (render_view) {
                render_view->move(
                    workspace_position.x() + kXOffset * static_cast<int>(index),
                    workspace_position.y() + kYOffset * static_cast<int>(index));
            }
        }
    }

    bool Workspace::eventFilter(QObject* watched, QEvent* event) {
        for (const auto render_view : render_views_) {
            if (!render_view) {
                continue;
            }
            
            if (render_view.get() == watched) {
                switch (event->type())
                {
                    case QEvent::Close: {
                        close_event_occurred_widget_ = render_view.get();
                        event->ignore();
                        this->close();
                        return true;
                    }
                }
            }
        }
        return QMainWindow::eventFilter(watched, event);
    }
}
