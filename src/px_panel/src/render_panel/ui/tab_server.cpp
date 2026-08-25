//
// Created by RGAA on 2024/4/9.
//

#include "tab_server.h"
#include <QScrollBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QLineEdit>
#include <utility>
#include <QPushButton>
#include <QComboBox>
#include <QRegExp>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QClipboard>
#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QUrl>
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_app_messages.h"
#include "px_common_new/qrcode/qr_generator.h"
#include "px_qt_widget/widget_helper.h"
#include "px_qt_widget/no_margin_layout.h"
#include "px_qt_widget/round_img_display.h"
#include "rn_app.h"
#include "rn_empty.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/log.h"
#include "qt_circle.h"
#include "px_dialog.h"
#include "render_panel/px_statistics.h"
#include "render_panel/px_application.h"
#include "px_qt_widget/sized_msg_box.h"
#include "render_panel/px_render_controller.h"
#include "service/service_manager.h"
#include "px_common_new/uid_spacer.h"
#include "render_panel/devices/stream_content.h"
#include "px_qt_widget/px_qr_widget.h"
#include "px_qt_widget/px_font_manager.h"
#include "px_qt_widget/px_label.h"
#include "px_qt_widget/px_pushbutton.h"
#include "px_qt_widget/px_image_button.h"
#include "px_qt_widget/px_circle_indicator.h"
#include "px_console_client/console_device_api.h"
#include "px_console_client/console_device.h"
#include "px_common_new/base64.h"
#include "px_common_new/px_aes.h"
#include <nlohmann/json.hpp>
#include "render_panel/database/stream_db_operator.h"
#include "render_panel/px_workspace.h"
#include "relay_message.pb.h"
#include "render_panel/companion/panel_companion.h"
#include "render_panel/px_statistics.h"
#include "render_panel/devices/px_device_manager.h"
#include "render_panel/devices/connection_policy.h"
#include "px_common_new/const_auto.h"

namespace px
{

    TabServer::TabServer(const std::shared_ptr<PxApplication>& app, QWidget *parent) : TabBase(app, parent) {
        settings_ = PxSettings::Instance();
        stat_ = PxStatistics::Instance();
        stream_db_mgr_ = context_->GetStreamDBManager();

        UpdateQRCode();

        // root layout
        auto root_layout = new QVBoxLayout();
        WidgetHelper::ClearMargins(root_layout);

        // title margin
        root_layout->addSpacing(kTabContentMarginTop);

        // content layout
        auto content_layout = new QHBoxLayout();
        WidgetHelper::ClearMargins(content_layout);

        auto item_width = 210;

        // left part
        {
            auto left_root = new NoMarginVLayout();

            // This Device
            {
                auto title = new TcLabel(this);
                title->setFixedWidth(item_width);
                title->SetTextId("id_this_device");
                title->setAlignment(Qt::AlignLeft);
                title->setStyleSheet(R"(font-size: 21px; font-weight:700;)");
                left_root->addWidget(title, 0, Qt::AlignLeft);
            }

            auto machine_code_qr_layout = new NoMarginHLayout();
            left_root->addSpacing(18);
            left_root->addLayout(machine_code_qr_layout);
            content_layout->addSpacing(15);
            content_layout->addLayout(left_root);
            content_layout->addSpacing(5);

            // machine code
            {
                auto layout = new NoMarginVLayout();
                layout->addSpacing(10);

                // Machine Code //
                {
                    auto title = new TcLabel(this);
                    title->setFixedWidth(item_width);
                    title->SetTextId("id_device_id");
                    title->setAlignment(Qt::AlignLeft);
                    title->setStyleSheet(R"(font-size: 12px; font-weight:500;)");
                    //layout->addSpacing(2);
                    layout->addWidget(title, 0, Qt::AlignLeft);

                    auto code_layout = new NoMarginHLayout();

                    auto msg = new QLabel(this);
                    msg->setFixedWidth(160);
                    lbl_machine_code_ = msg;
                    msg->setTextInteractionFlags(Qt::TextSelectableByMouse);
                    //auto uid = QString::fromStdString(px::SpaceId(context_->GetSysUniqueId()));
                    msg->setText(px::SpaceId("---------").c_str());
                    msg->setStyleSheet(R"(font-size: 18px; font-weight: 700; color: #2979ff;)");
                    code_layout->addWidget(msg);

                    auto btn_cpy = new TcImageButton(":/resources/image/ic_copy.svg", QSize(20, 20));
                    btn_cpy->SetColor(0xffffff, 0xdddddd, 0xbbbbbb);
                    btn_cpy->SetRoundRadius(15);
                    btn_cpy->setFixedSize(30, 30);
                    code_layout->addSpacing(10);
                    code_layout->addWidget(btn_cpy, 0, Qt::AlignVCenter);
                    code_layout->addStretch();

                    layout->addSpacing(5);
                    layout->addLayout(code_layout);
                    machine_code_qr_layout->addLayout(layout);

                    btn_cpy->SetOnImageButtonClicked([=, this]() {
                        if (settings_->GetDeviceId().empty()) {
                            return;
                        }
                        QClipboard* clipboard = QApplication::clipboard();
                        clipboard->setText(msg->text());
                        context_->NotifyAppMessage(tcTr("id_copy_success"), tcTr("id_copy_success_clipboard"));
                    });
                }

                // Temporary Password
                {
                    layout->addSpacing(18);

                    auto title = new TcLabel(this);
                    title->setFixedWidth(230);
                    title->SetTextId("id_temporary_password");
                    title->setAlignment(Qt::AlignLeft);
                    title->setStyleSheet(R"(font-size: 12px; font-weight:500;)");
                    //layout->addSpacing(2);
                    layout->addWidget(title, 0, Qt::AlignLeft);

                    auto pwd_layout = new NoMarginHLayout();
                    auto msg = new QLabel(this);
                    msg->setFixedWidth(160);
                    lbl_machine_random_pwd_ = msg;
                    msg->setTextInteractionFlags(Qt::TextSelectableByMouse);
                    msg->setText("********");
                    msg->setStyleSheet(R"(font-size: 18px; font-weight: 700; color: #2979ff;)");
                    pwd_layout->addWidget(msg);

                    auto btn_refresh = new TcImageButton(":/resources/image/ic_refresh.svg", QSize(20, 20));
                    btn_refresh->SetColor(0xffffff, 0xdddddd, 0xbbbbbb);
                    btn_refresh->SetRoundRadius(15);
                    btn_refresh->setFixedSize(30, 30);
                    pwd_layout->addSpacing(10);
                    pwd_layout->addWidget(btn_refresh, 0, Qt::AlignVCenter);

                    auto btn_hide_pwd = new TcImageButton(":/resources/image/ic_pwd_visibility_on.svg", ":/resources/image/ic_pwd_visibility_off.svg", QSize(20, 20));
                    btn_hide_random_pwd_ = btn_hide_pwd;
                    btn_hide_pwd->SetColor(0xffffff, 0xdddddd, 0xbbbbbb);
                    btn_hide_pwd->SetRoundRadius(15);
                    btn_hide_pwd->setFixedSize(30, 30);
                    pwd_layout->addSpacing(2);
                    pwd_layout->addWidget(btn_hide_pwd, 0, Qt::AlignVCenter);

                    pwd_layout->addStretch();

                    layout->addSpacing(5);
                    //layout->addWidget(msg, 0, Qt::AlignLeft);
                    layout->addLayout(pwd_layout);
                    layout->addStretch();
                    machine_code_qr_layout->addLayout(layout);

                    // event
                    btn_refresh->SetOnImageButtonClicked([=, this]() {
                        if (settings_->GetDeviceId().empty()) {
                            return;
                        }
                        context_->PostTask([=, this]() {
                            auto opt_device = px_console::ConsoleDeviceApi::UpdateRandomPwd(settings_->GetConsoleServerHost(),
                                                                             settings_->GetConsoleServerPort(),
                                                                             grApp->GetAppkey(),
                                                                             settings_->GetDeviceId());
                            if (!opt_device.has_value()) {
                                LOGE("Refresh random password failed, code: {}", (int)opt_device.error());
                                return;
                            }
                            auto device = opt_device.value();
                            if (!device) {
                                LOGE("Refresh random password failed, nullptr.");
                                return;
                            }
                            settings_->SetDeviceId(device->device_id_);
                            if (cat comp = grApp->GetCompanion(); comp) {
                                comp->UpdateDeviceId(device->device_id_);
                            }
                            settings_->SetDeviceRandomPwd(device->gen_random_pwd_);

                            context_->SendAppMessage(MsgRandomPasswordUpdated {
                                .device_id_ = device->device_id_,
                                .device_random_pwd_ = device->gen_random_pwd_,
                            });

                            context_->SendAppMessage(MsgSyncSettingsToRender{});
                        });
                    });

                    btn_hide_pwd->SetOnImageButtonClicked([=, this]() {
                        if (settings_->GetDeviceRandomPwd().empty()) {
                            return;
                        }
                        settings_->SetDisplayRandomPwd(!settings_->IsDisplayRandomPwd());
                        SetDeviceRandomPwdVisibility();
                    });
                }

                {
                    auto title = new TcLabel(this);
                    title->setFixedWidth(item_width);
                    title->SetTextId("id_device_name");
                    title->setAlignment(Qt::AlignLeft);
                    title->setStyleSheet(R"(font-size: 12px; font-weight:500;)");
                    layout->addSpacing(18);
                    layout->addWidget(title, 0, Qt::AlignLeft);

                    auto code_layout = new NoMarginHLayout();

                    auto msg = new QLineEdit(this);
                    edt_machine_name_ = msg;
                    msg->setFixedSize(160, 35);
                    //auto uid = QString::fromStdString(px::SpaceId(context_->GetSysUniqueId()));
                    msg->setText("");
                    msg->setStyleSheet(R"(font-size: 18px; font-weight: 700; color: #2979ff;)");
                    code_layout->addWidget(msg);

                    auto btn_save = new TcImageButton(":/resources/image/ic_save.svg", QSize(20, 20));
                    btn_save->SetColor(0xffffff, 0xdddddd, 0xbbbbbb);
                    btn_save->SetRoundRadius(15);
                    btn_save->setFixedSize(30, 30);
                    code_layout->addSpacing(10);
                    code_layout->addWidget(btn_save, 0, Qt::AlignVCenter);
                    code_layout->addStretch();

                    layout->addSpacing(5);
                    layout->addLayout(code_layout);
                    machine_code_qr_layout->addLayout(layout);

                    btn_save->SetOnImageButtonClicked([=, this]() {
                        if (msg->text().isEmpty()) {
                            return;
                        }
                        auto device_mgr = grApp->GetDeviceManager();
                        auto r = device_mgr->UpdateDeviceName(msg->text().toStdString());
                        if (r.has_value()) {
                            settings_->SetDeviceName(r.value()->device_name_);
                            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_update_success"));
                        }
                        else {
                            TcDialog dialog(tcTr("id_error"), tcTr("id_op_error"), this);
                            dialog.exec();
                        }
                    });
                }
            }

            {
                auto layout = new NoMarginVLayout();

                auto qr_info = new TcQRWidget(this);
                //qr_info->setFixedSize(171, 171);
                lbl_qr_code_ = qr_info;
                lbl_qr_code_->setFixedSize(qr_pixmap_.width()*3, qr_pixmap_.height()*3);
                qr_info->SetQRPixmap(qr_pixmap_);
                layout->addWidget(qr_info);
                layout->addStretch();
                machine_code_qr_layout->addLayout(layout);

                int size = 18;
                //auto img_path = std::format(":/icons/{}.png", context_->GetIndexByUniqueId());
                auto img_path = ":/resources/px_icon.png";
                auto avatar = new RoundImageDisplay(img_path, size, size, 4);
                qr_avatar_ = avatar;
                avatar->setParent(qr_info);
                avatar->setGeometry((qr_info->width()-size)/2, (qr_info->height()-size)/2, size, size);
            }

            // Connect Information
            {
                auto title = new TcLabel(this);
                title->setFixedWidth(item_width+50);
                title->SetTextId("id_connect_information");
                title->setAlignment(Qt::AlignLeft);
                title->setStyleSheet(R"(font-size: 21px; font-weight:700;)");
                left_root->addSpacing(30);
                left_root->addWidget(title, 0, Qt::AlignLeft);
            }

            // link:// information
            {
                left_root->addSpacing(18);

                {
                    auto layout = new NoMarginHLayout();
                    layout->setAlignment(Qt::AlignVCenter);

                    auto title = new TcLabel(this);
                    title->setFixedWidth(160);
                    title->SetTextId("id_detailed_information");
                    title->setAlignment(Qt::AlignLeft);
                    title->setStyleSheet(R"(font-size: 12px; font-weight:500;)");
                    layout->addWidget(title, 0, Qt::AlignLeft);

                    left_root->addLayout(layout);
                }

                auto layout = new NoMarginHLayout();
                auto msg = new QLineEdit(this);
                lbl_detailed_info_ = msg;
                msg->setAlignment(Qt::AlignLeft);
                msg->setMinimumWidth(210);
                msg->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                msg->setFixedHeight(35);
                auto info = std::format("link://{}", Base64::Base64Encode(context_->MakeDesktopLinkMessage()));
                msg->setText(info.c_str());
                msg->setCursorPosition(0);
                msg->setStyleSheet(R"(font-size: 12px; padding-left: 5px; font-weight: 500; color: #2979ff;)");
                msg->setEnabled(false);
                layout->addWidget(msg);

                layout->addSpacing(7);

                auto btn_conn = new TcPushButton();
                btn_conn->SetTextId("id_copy");
                btn_conn->setFixedWidth(55);
                btn_conn->setFixedHeight(35);
                layout->addWidget(btn_conn);
                connect(btn_conn, &QPushButton::clicked, this, [=, this]() {
                    QClipboard* clipboard = QApplication::clipboard();
                    clipboard->setText(msg->text());
                    context_->NotifyAppMessage(tcTr("id_copy_success"), tcTr("id_copy_success_clipboard"));
                });

                left_root->addSpacing(5);
                left_root->addLayout(layout);
            }

            // Web client 网页客户端地址(可复制的直连 URL)
            {
                left_root->addSpacing(13);

                {
                    auto layout = new NoMarginHLayout();
                    layout->setAlignment(Qt::AlignVCenter);

                    auto title = new TcLabel(this);
                    title->setFixedWidth(160);
                    title->SetTextId("id_web_client_addr");
                    title->setAlignment(Qt::AlignLeft);
                    title->setStyleSheet(R"(font-size: 12px; font-weight:500;)");
                    layout->addWidget(title, 0, Qt::AlignLeft);

                    left_root->addLayout(layout);
                }

                auto layout = new NoMarginHLayout();
                auto msg = new QLineEdit(this);
                edt_web_client_url_ = msg;
                msg->setAlignment(Qt::AlignLeft);
                msg->setMinimumWidth(210);
                msg->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                msg->setFixedHeight(35);
                msg->setStyleSheet(R"(font-size: 12px; padding-left: 5px; font-weight: 500; color: #2979ff;)");
                msg->setEnabled(false);
                layout->addWidget(msg);
                UpdateWebClientUrl();

                layout->addSpacing(7);

                auto btn_conn = new TcPushButton();
                btn_conn->SetTextId("id_copy");
                btn_conn->setFixedWidth(55);
                btn_conn->setFixedHeight(35);
                layout->addWidget(btn_conn);
                connect(btn_conn, &QPushButton::clicked, this, [=, this]() {
                    QClipboard* clipboard = QApplication::clipboard();
                    clipboard->setText(msg->text());
                    context_->NotifyAppMessage(tcTr("id_copy_success"), tcTr("id_copy_success_clipboard"));
                });

                layout->addSpacing(6);

                auto btn_open = new TcPushButton();
                btn_open->SetTextId("id_open");
                btn_open->setFixedWidth(55);
                btn_open->setFixedHeight(35);
                layout->addWidget(btn_open);
                connect(btn_open, &QPushButton::clicked, this, [=, this]() {
                    QDesktopServices::openUrl(QUrl(msg->text()));
                });

                left_root->addSpacing(5);
                left_root->addLayout(layout);
            }

            // Remote Device
            {
                auto title = new TcLabel(this);
                title->setFixedWidth(item_width);
                title->SetTextId("id_remote_device");
                title->setAlignment(Qt::AlignLeft);
                title->setStyleSheet(R"(font-size: 21px; font-weight:700;)");
                left_root->addSpacing(30);
                left_root->addWidget(title, 0, Qt::AlignLeft);
            }

            left_root->addSpacing(18);

            // remote machine code
            {
                auto remote_input_width = 160;
                auto remote_input_layout = new NoMarginHLayout();

                // Machine Code //
                {
                    auto input_layout = new NoMarginVLayout();
                    auto title = new TcLabel(this);
                    title->setFixedWidth(remote_input_width);
                    title->SetTextId("id_remote_device_id");
                    title->setAlignment(Qt::AlignLeft);
                    title->setStyleSheet(R"(font-size: 12px; font-weight:500;)");
                    input_layout->addWidget(title, 0, Qt::AlignLeft);

                    auto remote_codes = new QComboBox(this);
                    remote_devices_ = remote_codes;
                    remote_codes->setValidator(new QIntValidator(this));
                    remote_codes->setFixedWidth(remote_input_width);
                    remote_codes->setFixedHeight(35);
                    remote_codes->setStyleSheet(R"(font-size: 16px; font-weight: 700; color: #2979ff;)");
                    remote_codes->setEditable(true);

                    recent_streams_ = stream_db_mgr_->GetStreamsSortByCreatedTime(1, 20, false);
                    for (auto& stream : recent_streams_) {
                        if (connection_policy::IsConsoleTicket(stream->connect_type_)
                            && !stream->remote_device_id_.empty()
                            && remote_codes->findText(stream->remote_device_id_.c_str()) < 0) {
                            remote_codes->addItem(stream->remote_device_id_.c_str());
                        }
                    }

                    input_layout->addSpacing(5);
                    input_layout->addWidget(remote_codes, 0, Qt::AlignLeft);
                    remote_input_layout->addLayout(input_layout);
                }

                // connect
                {
                    auto input_layout = new NoMarginVLayout();

                    auto title = new TcLabel(this);
                    title->setFixedWidth(1);
                    title->setAlignment(Qt::AlignLeft);
                    title->setStyleSheet(R"(font-size: 12px; font-weight:500;)");
                    input_layout->addWidget(title, 0, Qt::AlignLeft);

                    auto btn_conn = new TcPushButton();
                    btn_conn->setFixedWidth(80);
                    btn_conn->setFixedHeight(35);
                    btn_conn->SetTextId("id_connect");
                    input_layout->addWidget(btn_conn);
                    remote_input_layout->addSpacing(8);
                    remote_input_layout->addLayout(input_layout);

                    connect(btn_conn, &QPushButton::clicked, this, [=, this]() {
                        auto remote_device_id = remote_devices_->currentText().replace(" ", "").trimmed().toStdString();
                        if (remote_device_id.empty()) {
                            return;
                        }

                        std::shared_ptr<px_console::ConsoleStream> item = std::make_shared<px_console::ConsoleStream>();
                        item->stream_id_ = "console-device-" + remote_device_id;
                        item->stream_name_ = remote_device_id;
                        item->encode_bps_ = 0;
                        item->encode_fps_ = 0;
                        item->remote_device_id_ = remote_device_id;
                        item->connect_type_ = connection_policy::kConsoleDeviceTicket;
                        item->bg_color_ = 0xffffff;
                        context_->SendAppMessage(StreamItemAdded {
                            .item_ = item,
                            .auto_start_ = true,
                        });
                    });
                }

                left_root->addLayout(remote_input_layout);
            }
            left_root->addStretch(120);

            // server status of myself
            {
                auto layout = new NoMarginHLayout();
                layout->setAlignment(Qt::AlignHCenter);
                auto label_size = QSize(75, 30);
                auto indicator_size = QSize(14, 14);
                {
                    // indicator
                    auto indicator = new TcCircleIndicator(this);
                    console_indicator_ = indicator;
                    indicator->setFixedSize(indicator_size);
                    layout->addWidget(indicator);

                    layout->addSpacing(5);

                    // text
                    auto text = new TcLabel(this);
                    text->setFixedSize(label_size);
                    text->SetTextId("id_manager_server");
                    layout->addWidget(text);
                }

                {
                    auto indicator = new TcCircleIndicator(this);
                    relay_indicator_ = indicator;
                    indicator->setFixedSize(indicator_size);
                    layout->addWidget(indicator);

                    layout->addSpacing(5);

                    // text
                    auto text = new TcLabel(this);
                    text->setFixedSize(label_size);
                    text->SetTextId("id_relay_server");
                    layout->addWidget(text);
                }

                {
                    auto indicator = new TcCircleIndicator(this);
                    relay_ft_indicator_ = indicator;
                    indicator->setFixedSize(indicator_size);
                    layout->addWidget(indicator);

                    layout->addSpacing(5);

                    // text
                    auto text = new TcLabel(this);
                    text->setFixedSize(label_size);
                    text->SetTextId("id_relay_ft_server");
                    layout->addWidget(text);
                }


                layout->addStretch();

                left_root->addLayout(layout);
                left_root->addSpacing(10);
            }
        }

        // clients
        {
            stream_content_ = new StreamContent(context_, this);
            stream_content_->setMinimumWidth(800);
            content_layout->addWidget(stream_content_);
        }

        root_layout->addLayout(content_layout);
        setLayout(root_layout);

        // set client id by settings
        if (!settings_->GetDeviceId().empty() && !settings_->GetDeviceRandomPwd().empty()) {
            lbl_machine_code_->setText(px::SpaceId(settings_->GetDeviceId()).c_str());
            //lbl_machine_random_pwd_->setText(settings_->GetDeviceRandomPwd().c_str());
            SetDeviceRandomPwdVisibility();
        }

        edt_machine_name_->setText(settings_->GetDeviceName().c_str());

        RegisterMessageListener();
    }

    TabServer::~TabServer() = default;

    void TabServer::OnTabShow() {
        TabBase::OnTabShow();
    }

    void TabServer::OnTabHide() {
        TabBase::OnTabHide();
    }

    void TabServer::RegisterMessageListener() {
        msg_listener_ = context_->ObtainUIMessageListener();

        // new device created
        msg_listener_->Listen<MsgRequestedNewDevice>([=, this](const MsgRequestedNewDevice& msg) {
            context_->PostUITask([=, this]() {
                lbl_machine_code_->setText(px::SpaceId(msg.device_id_).c_str());
                edt_machine_name_->setText(settings_->GetDeviceName().c_str());
                //lbl_machine_random_pwd_->setText(msg.device_random_pwd_.c_str());
                SetDeviceRandomPwdVisibility();
                this->UpdateQRCode();
            });
        });

        // random password updated
        msg_listener_->Listen<MsgRandomPasswordUpdated>([=, this](const MsgRandomPasswordUpdated& msg) {
            context_->PostUITask([=, this]() {
                lbl_machine_code_->setText(px::SpaceId(msg.device_id_).c_str());
                //lbl_machine_random_pwd_->setText(msg.device_random_pwd_.c_str());
                SetDeviceRandomPwdVisibility();
                this->UpdateQRCode();
            });
        });

        // program data cleared
        msg_listener_->Listen<MsgForceClearProgramData>([=, this](const MsgForceClearProgramData& msg) {
            context_->PostUITask([=, this]() {
                lbl_machine_code_->setText(px::SpaceId("---------").c_str());
                edt_machine_name_->setText("");
                SetDeviceRandomPwdVisibility();
                this->UpdateQRCode();
            });
        });

        msg_listener_->Listen<MsgGrTimer1S>([=, this](const MsgGrTimer1S& m) {
            context_->PostUITask([=, this]() {
                UpdateServerState();
            });
        });
    }

    void TabServer::UpdateQRCode() {
        auto broadcast_msg = context_->MakeDesktopLinkMessage();
        auto desktop_link_raw = broadcast_msg;
        auto b64_msg = Base64::Base64Encode(broadcast_msg);
        // companion
        //if (grApp->GetCompanion()) {
            //std::vector<uint8_t> enc_data;
            //if (grApp->GetCompanion()->EncQRCode(broadcast_msg, enc_data)) {
            //    broadcast_msg = Base64::Base64Encode(enc_data.data(), enc_data.size());
            //}
        //}

        auto qr_image = QrGenerator::GenQRImage(b64_msg, -1);
        QImage qimg(qr_image.rgba.data(), qr_image.width, qr_image.height, QImage::Format_RGBA8888);
        qr_pixmap_ = QPixmap::fromImage(qimg);
        LOGI("QR str: {}", b64_msg);
        LOGI("QR pixmap size: {}x{}", qr_pixmap_.width(), qr_pixmap_.height());
        //qr_pixmap_ = qr_pixmap_.scaled(60, 60, Qt::KeepAspectRatio, Qt::FastTransformation);
        if (lbl_qr_code_) {
            lbl_qr_code_->setFixedSize(qr_pixmap_.width()*3, qr_pixmap_.height()*3);
            lbl_qr_code_->SetQRPixmap(qr_pixmap_);
            qr_avatar_->setGeometry((lbl_qr_code_->width()-qr_avatar_->width())/2,
                                    (lbl_qr_code_->height()-qr_avatar_->height())/2,
                                    qr_avatar_->width(),
                                    qr_avatar_->height());
        }


        auto desktop_link = std::format("link://{}", b64_msg);
        if (lbl_detailed_info_) {
            lbl_detailed_info_->setText(desktop_link.c_str());
            lbl_detailed_info_->setCursorPosition(0);
        }

        // 设备 ID / 临时密码变化时同步刷新网页客户端直连地址
        UpdateWebClientUrl();

        context_->PostTask([=, this]() {
            auto dev_mgr = grApp->GetDeviceManager();
            dev_mgr->UpdateDesktopLink(desktop_link, desktop_link_raw);
        });

    }

    void TabServer::UpdateWebClientUrl() {
        if (!edt_web_client_url_) {
            return;
        }
        std::string ip;
        auto ips = context_->GetIps();
        if (!ips.empty()) {
            ip = ips[0].ip_addr_;
        }
        // 与 web/px_web_client connect_token 对齐:?c= URL-safe Base64(JSON{d,p})
        // 避免地址栏直接暴露 deviceId/password 明文
        nlohmann::json payload;
        payload["d"] = settings_->GetDeviceId();
        payload["p"] = settings_->GetDeviceRandomPwd();
        auto b64 = Base64::Base64Encode(payload.dump());
        for (char& ch : b64) {
            if (ch == '+') ch = '-';
            else if (ch == '/') ch = '_';
        }
        while (!b64.empty() && b64.back() == '=') {
            b64.pop_back();
        }
        auto url = std::format("http://{}:{}/web_client/?c={}",
                               ip, settings_->GetRenderServerPort(), b64);
        edt_web_client_url_->setText(QString::fromStdString(url));
        edt_web_client_url_->setCursorPosition(0);
    }

    void TabServer::resizeEvent(QResizeEvent *event) {
        TabBase::resizeEvent(event);
    }

    void TabServer::SetDeviceRandomPwdVisibility() {
        if (settings_->IsDisplayRandomPwd() && !settings_->GetDeviceRandomPwd().empty()) {
            lbl_machine_random_pwd_->setText(settings_->GetDeviceRandomPwd().c_str());
            btn_hide_random_pwd_->ToImage1();
        }
        else {
            lbl_machine_random_pwd_->setText("********");
            btn_hide_random_pwd_->ToImage2();
        }
    }

    void TabServer::UpdateServerState() {
        bool console_client_alive = grApp->IsConsoleClientAlive();
        console_indicator_->SetState(console_client_alive ? TcCircleIndicator::State::kOk : TcCircleIndicator::State::kError);
        auto device_id = settings_->GetDeviceId();
        if (!device_id.empty()) {
            auto current_ts = TimeUtil::GetCurrentTimestamp();
            // 心跳 1s 一次;阈值放宽到 10s,容忍会话建立/高负载时的短暂拥塞
            auto max_duration = 10000;
            {
                auto sid = "server_" + device_id;
                auto ts = stat_->GetRelayLastUpdateTimestamp(sid);
                //LOGI("relay alive: {}, ts: {}, diff: {}ms", sid, ts, (current_ts - ts));
                bool alive = current_ts - ts < max_duration;
                if (alive != last_relay_alive_) {
                    LOGI("relay indicator [{}] => {}", sid, alive ? "OK" : "ERROR");
                    last_relay_alive_ = alive;
                }
                relay_indicator_->SetState(alive ? TcCircleIndicator::State::kOk : TcCircleIndicator::State::kError);
            }
            {
                auto sid = "ft_server_" + device_id;
                auto ts = stat_->GetRelayLastUpdateTimestamp(sid);
                bool alive = current_ts - ts < max_duration;
                if (alive != last_relay_ft_alive_) {
                    LOGI("relay indicator [{}] => {}", sid, alive ? "OK" : "ERROR");
                    last_relay_ft_alive_ = alive;
                }
                relay_ft_indicator_->SetState(alive ? TcCircleIndicator::State::kOk : TcCircleIndicator::State::kError);
            }
        }
        else {
            relay_indicator_->SetState(TcCircleIndicator::State::kError);
            relay_ft_indicator_->SetState(TcCircleIndicator::State::kError);
        }
    }
}
