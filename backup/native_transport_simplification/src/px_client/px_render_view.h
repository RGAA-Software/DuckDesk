#pragma once

#include <QWidget>
#include <QLabel>
#include <QPointer>
#include <functional>
#include <memory>
#include <qevent.h>
#include "thunder_sdk.h"

#define TEST_SDL 0

namespace px
{

    class OpenGLVideoWidget;
    class SDLVideoWidget;
    class D3D11VideoWidget;
    class VideoWidget;
    class ClientContext;
    class ThunderSdk;
    class FloatController;
    class FloatControllerPanel;
    class MessageListener;
    class SvgLable;
    class Settings;
    class Thread;
    class MediaRecordSignLab;
    class OverlayWidget;

    class PxRenderView : public QWidget {
    public:
        PxRenderView(const std::shared_ptr<ClientContext>& ctx, std::shared_ptr<ThunderSdk>& sdk, const std::shared_ptr<ThunderSdkParams>& params, QWidget* parent);
        ~PxRenderView() override;
        void resizeEvent(QResizeEvent* event) override;
        void enterEvent(QEnterEvent* event) override;
        void leaveEvent(QEvent* event) override;
        bool eventFilter(QObject* watched, QEvent* event) override;
        bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
        void showEvent(QShowEvent* event) override;
        void hideEvent(QHideEvent* event) override;
        void moveEvent(QMoveEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void RefreshCapturedMonitorInfo(const SdkCaptureMonitorInfo& mon_info);
        void RefreshImage(const std::shared_ptr<RawImage>& image);
        void RefreshI420Image(const std::shared_ptr<RawImage>& image);
        void RefreshI444Image(const std::shared_ptr<RawImage>& image);
        void UpdateFullColorState(bool full_color);
        void SendKeyEvent(quint32 vk, bool down);
        void SetActiveStatus(bool active);
        bool GetActiveStatus() const;
        void SetMonitorName(const std::string& mon_name);
        void SwitchToFillWindow();
        void CalculateAspectRatio();
        void SetMainView(bool main_view);
        bool IsMainView() const;
        void SnapshotStream();
        void InitOverlayWidget();
        HWND GetVideoHwnd();
        std::string GetRenderTypeName();
        void UpdateOverlayWidgetPos();
    public:
        static bool s_mouse_in_;

    private:
        std::reference_wrapper<Settings> settings_;
        std::shared_ptr<VideoWidget> video_widget_;
    #if TEST_SDL
        std::shared_ptr<SDLVideoWidget> sdl_video_widget_;
    #endif
        std::shared_ptr<ClientContext> ctx_ = nullptr;
        std::shared_ptr<ThunderSdk> sdk_ = nullptr;
        std::shared_ptr<ThunderSdkParams> params_;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::string monitor_name_;
        bool active_ = false;
        bool is_main_view_ = false;

        QPointer<FloatController> float_controller_;
        QPointer<FloatControllerPanel> controller_panel_;

        QPointer<MediaRecordSignLab> recording_sign_lab_;

        bool need_recalculate_aspect_ = true;

        std::shared_ptr<Thread> thread_ = nullptr;

        QPointer<OverlayWidget> overlay_widget_;
    private:
        void InitFloatController();
        void RegisterControllerPanelListeners();
    };

}
