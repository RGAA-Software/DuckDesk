//
// Created by RGAA on 4/07/2024.
//

#ifndef GAMMARAYPC_FLOAT_CONTROLLER_PANEL_H
#define GAMMARAYPC_FLOAT_CONTROLLER_PANEL_H
#include <string>
#include <QPointer>
#include "float_overlay_window.h"
#include "px_client/ct_app_message.h"
#include "virtual_display_ui_state.h"

class QLabel;
class QPushButton;
class QTimer;

namespace px
{

    enum class SubPanelType {
        kWorkMode,
        kControl,
        kDisplay,
        kDebug,
    };

    class ComputerIcon;
    class FloatIcon;

    class FloatControllerPanel : public FloatOverlayWindow {
    public:
        explicit FloatControllerPanel(const std::shared_ptr<ClientContext>& ctx, QWidget* parent = nullptr);
        void paintEvent(QPaintEvent *event) override;

        void SetOnDebugListener(OnClickListener&& l) { debug_listener_ = l; }
        void SetOnFileTransListener(OnClickListener&& l) { file_trans_listener_ = l; }
        void SetOnMediaRecordListener(OnClickListener&& listener) { media_record_listener_ = listener; }
        void Hide() override;
        void SetMainControl();
        void SetMonitorName(const std::string& mon_name);
        void ToggleVoiceCall();
        void ToggleVoiceMicrophoneMute();
        void ToggleVoiceSpeakerMute();
        void SelectVoiceAudioDevices();
    private:
        BaseWidget* GetSubPanel(const SubPanelType& type);
        void ShowSubPanel(FloatOverlayWindow* panel, QWidget* anchor);
        void HideAllSubPanels();
        void UpdateCaptureMonitorInfo();
        void SwitchMonitor(ComputerIcon* w);
        void CaptureAllMonitor();
        void UpdateCapturingMonitor(const std::string& name, int cur_cap_mon_index);
        void UpdateStatus(const MsgClientFloatControllerPanelUpdate& msg) override;
        void StartVirtualDisplayRequest(VirtualDisplayUiOperation operation);
        void UpdateVirtualDisplayUi();
    private:
        OnClickListener debug_listener_;
        OnClickListener file_trans_listener_;
        OnClickListener media_record_listener_;
        std::map<SubPanelType, QPointer<BaseWidget>> sub_panels_;
        std::vector<ComputerIcon*> computer_icons_;
        MsgClientCaptureMonitor capture_monitor_;

        static constexpr int kInitialWidth = 320;

        // 分屏显示按钮
        FloatIcon* split_screen_btn_ = nullptr;

        FloatIcon* audio_btn_ = nullptr;
        FloatIcon* voice_call_btn_ = nullptr;
        FloatIcon* voice_audio_device_btn_ = nullptr;
        FloatIcon* voice_microphone_mute_btn_ = nullptr;
        FloatIcon* voice_speaker_mute_btn_ = nullptr;

        FloatIcon* full_screen_btn_ = nullptr;

        //是否是主窗口的控制面板
        bool is_main_control_ = false;

        std::string monitor_name_;

        QLabel* media_record_lab_ = nullptr;

        QLabel* virtual_display_label_ = nullptr;
        QPushButton* virtual_display_add_btn_ = nullptr;
        QPushButton* virtual_display_remove_btn_ = nullptr;
        QTimer* virtual_display_timeout_timer_ = nullptr;
        VirtualDisplayUiState virtual_display_ui_state_;
        VoiceCallPhase voice_call_phase_ = VoiceCallPhase::kIdle;
        bool voice_call_supported_ = false;
        bool voice_call_requires_headset_ = true;
        bool voice_call_warning_shown_ = false;
        bool voice_microphone_muted_ = false;
        bool voice_speaker_muted_ = false;
        std::string voice_capture_device_id_;
        std::string voice_playout_device_id_;
    };

}

#endif //GAMMARAYPC_FLOAT_CONTROLLER_PANEL_H
