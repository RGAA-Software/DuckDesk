#include "px_render_view.h"
#include <qsizepolicy.h>
#include <qpalette.h>
#include <QTimer>
#include <QPixmap>
#include <QDateTime>
#include <QStandardPaths>
#include <QMainWindow>
#include <QProcess>
#include <qlabel.h>
#include <qicon.h>
#include <qpointer.h>
#include <qpixmap.h>
#include "px_dialog.h"
#include "no_margin_layout.h"
#include "ct_const_def.h"
#include "ct_game_overlay.h"
#include "ui/float_controller.h"
#include "ui/float_controller_panel.h"
#include "ui/svg_lable.h"
#include "ui/media_record_sign_lab.h"
#include "ct_client_context.h"
#include "px_common_new/log.h"
#include "px_client/ct_settings.h"
#include "px_common_new/thread.h"
#include "px_common_new/time_util.h"
#include "px_common_new/file_util.h"
#include "px_client_sdk_new/sdk_messages.h"
#include "front_render/sdl/ct_sdl_video_widget.h"
#include "front_render/d3d11/ct_d3d11_video_widget.h"
#include "front_render/opengl/ct_opengl_video_widget.h"
#include "front_render/vulkan/ct_vulkan_video_widget.h"

namespace px
{

    bool PxRenderView::s_mouse_in_ = false;

    PxRenderView::PxRenderView(const std::shared_ptr<ClientContext>& ctx, std::shared_ptr<ThunderSdk>& sdk, const std::shared_ptr<ThunderSdkParams>& params, QWidget* parent)
        : QWidget(parent), settings_(*Settings::Instance()), ctx_(ctx), sdk_(sdk), params_(params) {
        WidgetHelper::SetTitleBarColor(this, this->params_->titlebar_color_);
        msg_listener_ = ctx_->ObtainUIMessageListener();
        this->setAttribute(Qt::WA_StyledBackground, true);
        auto beg = TimeUtil::GetCurrentTimestamp();

        this->thread_ = Thread::Make("d3d_render", 120);
        this->thread_->Poll();

    #if TEST_SDL
        if (parent) {
            sdl_video_widget_ = std::make_shared<SDLVideoWidget>(
                ctx, sdk_, 0, RawImageFormat::kRawImageI420, nullptr);
            sdl_video_widget_->setFixedSize(1280, 768);
            sdl_video_widget_->show();
        }
    #endif

#ifdef WIN32
        if (params_->support_vulkan_) {
            LOGI("*** Use vulkan to render frames");
            video_widget_ = std::make_shared<VulkanVideoWidget>(
                ctx, sdk_, 0, RawImageFormat::kRawImageVulkanAVFrame, this);
        }
        else {
            if (params_->d3d11_wrapper_) {
                video_widget_ = std::make_shared<D3D11VideoWidget>(
                    ctx, sdk_, 0, RawImageFormat::kRawImageD3D11Texture, this);
                LOGI("*** Use D3D11 to render frames");
            }
            else {
                video_widget_ = std::make_shared<OpenGLVideoWidget>(
                    ctx, sdk_, 0, RawImageFormat::kRawImageI420, this);
                LOGI("*** Use OpenGL to render frames");
            }
        }
#else
        video_widget_ = std::make_shared<OpenGLVideoWidget>(
            ctx, sdk_, 0, RawImageFormat::kRawImageI420, this);
#endif
        auto end = TimeUtil::GetCurrentTimestamp();
        LOGI("Create OpenGLWidget used: {}ms", (end-beg));

        auto size_policy = video_widget_->AsWidget()->sizePolicy();
        size_policy.setHorizontalPolicy(QSizePolicy::Expanding);
        size_policy.setVerticalPolicy(QSizePolicy::Expanding);
        video_widget_->AsWidget()->setSizePolicy(size_policy);

        InitFloatController();

        recording_sign_lab_ = new MediaRecordSignLab(ctx, this);
        recording_sign_lab_->move(this->width() * 0.85, 20);
        recording_sign_lab_->hide();
        const QPointer<PxRenderView> guarded_self(this);

        msg_listener_->Listen<MsgClientMediaRecord>([guarded_self](const MsgClientMediaRecord&) {
            if (!guarded_self) {
                return;
            }
            bool res = guarded_self->ctx_->GetRecording();
            if(res) {
                guarded_self->recording_sign_lab_->show();
                //animation->start(); // 开始动画
            }
            else {
                guarded_self->recording_sign_lab_->hide();
                //animation->stop();
            }
        });

        msg_listener_->Listen<MsgClientSwitchMonitor>([guarded_self](const MsgClientSwitchMonitor&) {
            if (guarded_self &&
                ScaleMode::kKeepAspectRatio == guarded_self->settings_.get().scale_mode_ &&
                !guarded_self->isHidden()) {
                guarded_self->need_recalculate_aspect_ = true;
            }
        });


        msg_listener_->Listen<MsgClientHidePanel>([guarded_self](const MsgClientHidePanel&) {
            if (guarded_self) {
                guarded_self->ctx_->PostUITask([guarded_self]() {
                    if (guarded_self) {
                        guarded_self->controller_panel_->Hide();
                    }
                });
            }
        });

        msg_listener_->Listen<SdkMsgTimer1000>([guarded_self](const SdkMsgTimer1000&) {
            if (guarded_self && guarded_self->video_widget_) {
                guarded_self->video_widget_->OnTimer1S();
            }
        });

        // 连接/重连成功后,补发按下中的键鼠 release,避免远端按键卡死、鼠标粘连
        msg_listener_->Listen<SdkMsgNetworkConnected>([guarded_self](const SdkMsgNetworkConnected&) {
            if (guarded_self) {
                guarded_self->ctx_->PostUITask([guarded_self]() {
                    if (guarded_self && guarded_self->video_widget_) {
                        guarded_self->video_widget_->ReleaseAllPressedInputs();
                    }
                });
            }
        });

        msg_listener_->Listen<MsgStreamShot>([guarded_self](const MsgStreamShot&) {
            if (guarded_self) {
                guarded_self->SnapshotStream();
            }
        });
    }

    PxRenderView::~PxRenderView() {
        if (msg_listener_) {
            msg_listener_->UnListenAll();
        }
    }

    void PxRenderView::resizeEvent(QResizeEvent* event) {
        const auto scale_mode = settings_.get().scale_mode_;
        if (scale_mode == ScaleMode::kFillWindow) {
            SwitchToFillWindow();
        }
        else if (scale_mode == ScaleMode::kKeepAspectRatio) {
            CalculateAspectRatio();
        }

        if (float_controller_) {
            float_controller_->ReCalculatePosition();
        }
        if (controller_panel_ && controller_panel_->isVisible()) {
            controller_panel_->Hide();
        }
        recording_sign_lab_->move(this->width() * 0.85, 20);
        if (overlay_widget_) {
            overlay_widget_->resize(this->size());
        }
        QWidget::resizeEvent(event);
    }

    void PxRenderView::RefreshImage(const std::shared_ptr<RawImage>& image) {
        if (video_widget_) {
            video_widget_->RefreshImage(image);
        }
        UpdateFullColorState(image->full_color_);
    }

    void PxRenderView::UpdateFullColorState(bool full_color) {
        if (!is_main_view_) {
            return;
        }
        if (settings_.get().IsFullColorEnabled() != full_color) {
            settings_.get().SetFullColorEnabled(full_color);
            ctx_->SendAppMessage(MsgClientFloatControllerPanelUpdate{
                .update_type_ = MsgClientFloatControllerPanelUpdate::EUpdate::kFullColorStatus
            });
        }
    }

    void PxRenderView::RefreshI420Image(const std::shared_ptr<RawImage>& image) {
        if (video_widget_->GetDisplayImageFormat() != kRawImageI420) {
            video_widget_->SetDisplayImageFormat(kRawImageI420);
        }
        video_widget_->RefreshImage(image);

    #if TEST_SDL
        const QPointer<PxRenderView> guarded_self(this);
        ctx_->PostUITask([guarded_self, image]() {
            if (guarded_self && guarded_self->sdl_video_widget_) {
                guarded_self->sdl_video_widget_->RefreshI420Image(image);
            }
        });
    #endif
    }

    void PxRenderView::RefreshI444Image(const std::shared_ptr<RawImage>& image) {
        if (video_widget_->GetDisplayImageFormat() != kRawImageI444) {
            video_widget_->SetDisplayImageFormat(kRawImageI444);
        }
        video_widget_->RefreshImage(image);
    }

    void PxRenderView::RefreshCapturedMonitorInfo(const SdkCaptureMonitorInfo& mon_info) {
        // 若按比例缩放的情况下，切换了屏幕，屏幕分辨率未必一致，如一个4K,一个2K，故重新计算
        if (need_recalculate_aspect_
            && ScaleMode::kKeepAspectRatio == settings_.get().scale_mode_) {
            const auto& exist_mon_info = video_widget_->GetCaptureMonitorInfo();
            if (mon_info.mon_name_ != exist_mon_info.mon_name_ && !exist_mon_info.mon_name_.empty()) {
                const QPointer<PxRenderView> guarded_self(this);
                ctx_->PostDelayUITask([guarded_self]() {
                    if (guarded_self) {
                        guarded_self->CalculateAspectRatio();
                    }
                }, 100);
                need_recalculate_aspect_ = false;
            }
        }
        video_widget_->RefreshCapturedMonitorInfo(mon_info);
    }

    void PxRenderView::SendKeyEvent(quint32 vk, bool down) {
        video_widget_->SendKeyEvent(vk, down);
    }

    void PxRenderView::SwitchToFillWindow() {
        auto target_title_bar_height = this->isFullScreen() ? 0 : kTitleBarHeight;
        video_widget_->AsWidget()->setGeometry(0, target_title_bar_height, this->width(), this->height() - kTitleBarHeight);
    }

    void PxRenderView::CalculateAspectRatio() {
        auto vw = video_widget_->GetCapturingMonitorWidth();
        auto vh = video_widget_->GetCapturingMonitorHeight();
        // no frame, fill the window
        if (vw <= 0 || vh <= 0) {
            video_widget_->AsWidget()->setGeometry(0, kTitleBarHeight, this->width(), this->height());
            return;
        }

        auto target_title_bar_height = this->isFullScreen() ? 0 : kTitleBarHeight;

        int available_height = this->height() - target_title_bar_height;
        float h_ratio = vw * 1.0f / this->width();
        float v_ratio = vh * 1.0f / available_height;//this->height();
        int target_width = 0;
        int target_height = 0;

        float widget_ratio = this->width() * 1.0f / available_height;
        float frame_ratio = vw * 1.0f / vh;
        if (widget_ratio > frame_ratio) {
            // along to height
            target_height = available_height;
            target_width = vw * (available_height * 1.0f / vh);
        }
        else {
            // along to width
            target_width = this->width();
            target_height = vh * (this->width() * 1.0f / vw);
        }

        video_widget_->AsWidget()->setGeometry((this->width() - target_width) / 2, (available_height - target_height) / 2 + target_title_bar_height, target_width, target_height);

    }

    void PxRenderView::InitFloatController()
    {
        float_controller_ = new FloatController(ctx_, this);
        float_controller_->installEventFilter(this);
        controller_panel_ = new FloatControllerPanel(ctx_, this);
        RegisterControllerPanelListeners();
        controller_panel_->installEventFilter(this);
        controller_panel_->hide();

        // The overlays are top-level native windows. Moving the outer client
        // window does not generate a move event for this child PxRenderView, so
        // observe the host window as well and keep the overlays attached.
        if (window() && window() != this) {
            window()->installEventFilter(this);
        }
        video_widget_->AsWidget()->installEventFilter(this);

        const QPointer<PxRenderView> guarded_self(this);
        float_controller_->SetOnClickListener([guarded_self]() {
            if (!guarded_self) {
                return;
            }
            if (guarded_self->controller_panel_->isHidden()) {
                if (!guarded_self->float_controller_->HasMoved()) {
                    guarded_self->controller_panel_->ShowBeside(
                        guarded_self->float_controller_->VisualRectGlobal(), true);
                }
            }
            else {
                guarded_self->controller_panel_->Hide();
            }
        });

        float_controller_->SetOnMoveListener([guarded_self]() {
            if (!guarded_self || !guarded_self->controller_panel_) {
                return;
            }
            guarded_self->controller_panel_->Hide();
        });
    }

    void PxRenderView::RegisterControllerPanelListeners() {
        const QPointer<PxRenderView> guarded_self(this);
        controller_panel_->SetOnDebugListener([guarded_self](auto) {
            if (!guarded_self) {
                return;
            }
            guarded_self->ctx_->PostUITask([guarded_self]() {
                if (guarded_self) {
                    guarded_self->controller_panel_->Hide();
                }
            });
            guarded_self->ctx_->SendAppMessage(MsgClientOpenDebugPanel{});
        });

        controller_panel_->SetOnFileTransListener([guarded_self](auto) {
            if (!guarded_self) {
                return;
            }
            guarded_self->ctx_->PostUITask([guarded_self]() {
                if (guarded_self) {
                    guarded_self->controller_panel_->Hide();
                }
            });
            guarded_self->ctx_->SendAppMessage(MsgClientOpenFiletrans{});
        });

        controller_panel_->SetOnMediaRecordListener([guarded_self](auto) {
            if (guarded_self) {
                guarded_self->ctx_->PostUITask([guarded_self]() {
                    if (guarded_self) {
                        guarded_self->controller_panel_->Hide();
                    }
                });
            }
        });
    }

    void PxRenderView::SetMainView(bool main_view) {
        is_main_view_ = main_view;
        if (is_main_view_ && controller_panel_) {
            controller_panel_->SetMainControl();
        }
    }

    void PxRenderView::SetMonitorName(const std::string& mon_name) {
        monitor_name_ = mon_name;
        controller_panel_->SetMonitorName(mon_name);
    }

    void PxRenderView::enterEvent(QEnterEvent* event) {
        s_mouse_in_ = true;
        this->ctx_->SendAppMessage(MsgClientMouseEnterView{});
        QWidget::enterEvent(event);
        //LOGI("PxRenderView, enterEvent.");
    }

    void PxRenderView::leaveEvent(QEvent* event) {
        s_mouse_in_ = false;
        this->ctx_->SendAppMessage(MsgClientMouseLeaveView{});
        QWidget::leaveEvent(event);
        //LOGI("PxRenderView, leaveEvent.");
    }

    bool PxRenderView::eventFilter(QObject* watched, QEvent* event) {
        if (watched == window() && watched != this) {
            switch (event->type()) {
            case QEvent::Move:
            case QEvent::Resize:
            case QEvent::Show:
            case QEvent::Hide:
            case QEvent::WindowStateChange: {
                if (controller_panel_) {
                    controller_panel_->Hide();
                }
                const QPointer<PxRenderView> guarded_self(this);
                QTimer::singleShot(0, this, [guarded_self]() {
                    const auto self = guarded_self;
                    if (!self || !self->float_controller_) {
                        return;
                    }
                    auto* host = self->window();
                    if (!host || !host->isVisible() || host->isMinimized() || !self->isVisible()) {
                        self->float_controller_->hide();
                        return;
                    }
                    self->float_controller_->ReCalculatePosition();
                    self->float_controller_->ShowWithoutActivating();
                });
                break;
            }
            default:
                break;
            }
        }
        if (watched == video_widget_->AsWidget() && event->type() == QEvent::KeyPress) {
            auto* key_event = static_cast<QKeyEvent*>(event);
            const auto modifiers = key_event->modifiers();
            const bool ctrl_alt_v = key_event->key() == Qt::Key_V &&
                modifiers.testFlag(Qt::ControlModifier) &&
                modifiers.testFlag(Qt::AltModifier);
            if (ctrl_alt_v || key_event->key() == Qt::Key_F10) {
                controller_panel_->ToggleVoiceCall();
                event->accept();
                return true;
            }
        }
        if (watched == controller_panel_ || watched == float_controller_)
        {
            switch (event->type())
            {
            case QEvent::Enter:
                s_mouse_in_ = false;
                this->ctx_->SendAppMessage(MsgClientMouseLeaveView{});
                break;
            case QEvent::Leave:
                s_mouse_in_ = true;
                this->ctx_->SendAppMessage(MsgClientMouseEnterView{});
                break;
            default:
                break;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    bool PxRenderView::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef WIN32
            MSG* msg = static_cast<MSG*>(message);
            if (msg->message == WM_ACTIVATE) {
                if (LOWORD(msg->wParam) == WA_INACTIVE) {
                    qDebug() << "Window lost focus!";
                    const auto context = ctx_;
                    ctx_->PostTask([context]() {
                        context->SendAppMessage(MsgClientFocusOutEvent{});
                    });
                }
                else {
                    qDebug() << "Window gained focus!";
                }
            }
            else if (msg->message == WM_EXITSIZEMOVE) {
                UpdateOverlayWidgetPos();
            }
#endif
        return QWidget::nativeEvent(eventType, message, result);
    }

    void PxRenderView::SetActiveStatus(bool active) {
        active_ = active;
    }

    bool PxRenderView::GetActiveStatus() const {
        return active_;
    }

    bool PxRenderView::IsMainView() const {
        return is_main_view_;
    }

    void PxRenderView::SnapshotStream() {
        if (this->isHidden()) {
            return;
        }
        auto image = video_widget_->CaptureImage();
        if (image.isNull()) {
            return;
        }

        QString name = windowTitle();
        if (name.isEmpty()) {
            if (const QPointer<QMainWindow> parent_window(
                    qobject_cast<QMainWindow*>(parentWidget())); parent_window) {
                name = parent_window->windowTitle();
            }
        }
        if (name.isEmpty()) {
            name = QString::fromStdString("Default");
        }
        name = name.replace("(", "").replace(")", "").replace(":", "_");
        QString png_name = name + "_" + QString::number(QDateTime::currentSecsSinceEpoch()) + ".png";
        QString pic_path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        QString png_path = pic_path + "/" + png_name;
        if (image.save(png_path)) {
            auto callback = [=]() {
                auto path = png_path;
                FileUtil::SelectFileInExplorer(std::filesystem::path(path.toStdWString()));
            };
            ctx_->NotifyAppMessage("Snap Success", std::format("Saved to: {}", pic_path.toStdString()).c_str(), callback);
        }
    }

    HWND PxRenderView::GetVideoHwnd() {
        return (HWND)video_widget_->GetRenderWId();
    }

    void PxRenderView::showEvent(QShowEvent* event) {
        QWidget::showEvent(event);
        // 非主窗口创建出来后，并没有显示出来, 需要触发一次 resizeEvent 来更新交换链信息
        if (width() > 0 && height() > 0) {
            resize(width() + 1, height() + 1);
        }
        const QPointer<PxRenderView> guarded_self(this);
        QTimer::singleShot(60, this, [guarded_self]() {
            const auto self = guarded_self;
            if (!self) {
                return;
            }
            self->resize(self->width() - 1, self->height() - 1);
        });
        if (overlay_widget_) {
            overlay_widget_->show();
        }
        QTimer::singleShot(0, this, [guarded_self]() {
            const auto self = guarded_self;
            if (!self || self->isHidden() || !self->float_controller_) {
                return;
            }
            self->float_controller_->ReCalculatePosition();
            self->float_controller_->ShowWithoutActivating();
        });
    }

    void PxRenderView::hideEvent(QHideEvent* event) {
        QWidget::hideEvent(event);
        if (overlay_widget_) {
            overlay_widget_->hide();
        }
        if (controller_panel_) {
            controller_panel_->Hide();
        }
        if (float_controller_) {
            float_controller_->hide();
        }
    }

    std::string PxRenderView::GetRenderTypeName() {
        if (!video_widget_) {
            return "";
        }
        return video_widget_->GetRenderTypeName();
    }

    void PxRenderView::moveEvent(QMoveEvent* event) {
        if (overlay_widget_) {
            QPoint global_pos = mapToGlobal(QPoint(0, 0));
            overlay_widget_->move(global_pos);
        }
        if (controller_panel_) {
            controller_panel_->Hide();
        }
        if (float_controller_) {
            float_controller_->ReCalculatePosition();
        }
        QWidget::moveEvent(event);
    }

    void PxRenderView::InitOverlayWidget() {
        overlay_widget_ = new OverlayWidget(this);
        overlay_widget_->resize(this->size());
        overlay_widget_->SetOpacity(0.5);
        // overlay_widget_->SetWatermarkCount(10);
        overlay_widget_->hide();
        const QPointer<PxRenderView> guarded_self(this);
        QTimer::singleShot(1000, this, [guarded_self]() {
            if (guarded_self && guarded_self->overlay_widget_) {
                guarded_self->UpdateOverlayWidgetPos();
                if (guarded_self->isHidden()) {
                    guarded_self->overlay_widget_->hide();
                }
                else {
                    guarded_self->overlay_widget_->show();
                }
                // if (settings_.get().force_direct_) {
                //     overlay_widget_->SetWatermarkText("Force Direct");
                // }
                // if (settings_.get().show_watermark_) {
                //     overlay_widget_->SetWatermarkText("Unlicensed Stream");
                // }
                // if (settings_.get().show_watermark_ || settings_.get().force_direct_) {
                //     overlay_widget_->SetWatermarkCount(15);
                // }
                // else {
                //     overlay_widget_->SetWatermarkCount(0);
                // }
            }
        });
    }

    void PxRenderView::mouseReleaseEvent(QMouseEvent* event) {
        QWidget::mouseReleaseEvent(event);
    }

    void PxRenderView::UpdateOverlayWidgetPos() {
        if (overlay_widget_) {
            QPoint global_pos = mapToGlobal(QPoint(0, 0));
            overlay_widget_->resize(this->size());
            overlay_widget_->move(global_pos);
        }
    }
}
