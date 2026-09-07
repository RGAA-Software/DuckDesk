#pragma once 

#include <QWidget>
#include <qpainter.h>
#include <qevent.h>
#include <QPointer>
#include <functional>
class QCheckBox;

namespace pxrp
{
    class RpConnectedClientInfo;
}

namespace px {

    class NoMarginVLayout;
    class NoMarginHLayout;
    class TcLabel;
    class TcPushButton;
    class PxContext;
    class PxSettings;

    // 被客户端连接上来后，显示连接者的一些信息
    class ConnectedInfoPanel : public QWidget {
        Q_OBJECT
    public:
        ConnectedInfoPanel(const std::shared_ptr<PxContext>& ctx, QWidget* parent = nullptr);
        void paintEvent(QPaintEvent* event) override;
        void UpdateInfo(const std::shared_ptr<pxrp::RpConnectedClientInfo>& info);
        std::string GetStreamId() const;
    private:
        void InitView();
        void InitData();
        void InitSigChannel();
        void ShowAccessHint();
    private:
        QPointer<NoMarginVLayout> root_vbox_layout_;

        QPointer<NoMarginHLayout> logo_hbox_layout_;
        QPointer<TcLabel> logo_lab_;
        QPointer<TcLabel> logo_name_lab_;

        QPointer<NoMarginHLayout> avatar_name_hbox_layout_;
        // 头像
        QPointer<TcLabel> avatar_lab_;
        // 名字
        QPointer<TcLabel> key_1_lab_;
        QPointer<TcLabel> key_2_lab_;
        QPointer<TcLabel> conn_prompt_lab_;
        // 断开连接
        QPointer<TcPushButton> disconnect_btn_;

        QPointer<NoMarginHLayout> promtp_hbox_layout_;
        QPointer<TcLabel> prompt_lab_;
        // 提示用户去设置面板界面
        QPointer<TcLabel> access_hint_lab_;

        QPointer<NoMarginHLayout> access_control_hbox_layout_;
        // 声音
        QPointer<QCheckBox> voice_cbox_;
        QPointer<TcLabel> voice_lab_;
        // 键鼠
        QPointer<QCheckBox> key_mouse_cbox_;
        QPointer<TcLabel> key_mouse_lab_;
        // 文件
        QPointer<QCheckBox> file_cbox_;
        QPointer<TcLabel> file_lab_;
        
        std::shared_ptr<PxContext> ctx_ = nullptr;
        std::shared_ptr<pxrp::RpConnectedClientInfo> info_ = nullptr;
        std::reference_wrapper<PxSettings> settings_;
    };


}
