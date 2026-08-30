//
// Created by RGAA on 2024-06-10.
//

#include "st_security.h"
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDebug>
#include <QFileDialog>
#include <QStandardPaths>
#include <QPointer>
#include "px_dialog.h"
#include "px_label.h"
#include "px_pushbutton.h"
#include "px_qt_widget/no_margin_layout.h"
#include "render_panel/px_context.h"
#include "render_panel/px_application.h"
#include "render_panel/px_settings.h"
#include "px_qt_widget/sized_msg_box.h"
#include "px_common_new/win32/dxgi_mon_detector.h"
#include "px_common_new/log.h"
#include "px_common_new/string_util.h"
#include "px_common_new/win32/audio_device_helper.h"
#include "render_panel/px_app_messages.h"
#include "px_common_new/ip_util.h"
#include "px_console_client/console_device_api.h"
#include "input_safety_pwd_dialog.h"
#include "render_panel/devices/infinite_loading.h"
#include "px_qt_widget/px_dialog_util.h"
#include "px_common_new/folder_util.h"
#include "px_common_new/zip_util.h"
#include "px_common_new/file_util.h"
#include "render_panel/user/px_user_manager.h"
#include "render_panel/database/stream_db_operator.h"
#include "render_panel/ui/qt_lifetime_guard.h"

namespace px
{

    StSecurity::StSecurity(const std::shared_ptr<PxApplication>& app,
                           QWidget* parent) // NOLINT(gammaray-raw-pointer-boundary) Qt parent ABI; QWidget owns the child.
        : TabBase(app, parent),
          security_settings_(*PxSettings::Instance()) {
        auto root_layout = new NoMarginHLayout();
        auto column1_layout = new NoMarginVLayout();
        root_layout->addLayout(column1_layout);

        auto column2_layout = new NoMarginVLayout();
        root_layout->addSpacing(10);
        root_layout->addLayout(column2_layout);

        root_layout->addStretch();

        // segment encoder
        auto tips_label_width = 300;
        auto tips_label_height = 35;
        auto tips_label_size = QSize(tips_label_width, tips_label_height);
        auto input_size = QSize(280, tips_label_height);

        {
            auto segment_layout = new NoMarginVLayout();
            {
                // title
                auto label = new TcLabel(this);
                label->SetTextId("id_security_settings");
                label->setStyleSheet("font-size: 16px; font-weight: 700;");
                segment_layout->addSpacing(0);
                segment_layout->addWidget(label);
            }

            // Security password
            {
                auto layout = new NoMarginHLayout();
                auto label = new TcLabel(this);
                label->SetTextId("id_long_term_password");
                label->setFixedSize(tips_label_size);
                label->setStyleSheet("font-size: 14px; font-weight: 500;");
                layout->addWidget(label);

                auto edit = new TcPushButton(this);
                edit->SetTextId("id_set");
                edit->setFixedSize(QSize(80, 30));
                edit->setEnabled(true);
                layout->addWidget(edit, 0, Qt::AlignVCenter);
                layout->addStretch();
                segment_layout->addSpacing(5);
                segment_layout->addLayout(layout);
                const auto app_snapshot = app_;
                connect(edit, &QPushButton::clicked, this,
                        MakeQtLifetimeAction(QPointer<StSecurity>(this),
                            [app_snapshot](const QPointer<StSecurity>& self) {
                                InputSafetyPwdDialog dialog(app_snapshot, self);
                                dialog.exec();
                            }));
            }

            // Mouse&Keyboard
            {
                auto layout = new NoMarginHLayout();
                auto label = new TcLabel(this);
                label->SetTextId("id_allowed_mouse_keyboard");
                label->setFixedSize(tips_label_size);
                label->setStyleSheet("font-size: 14px; font-weight: 500;");
                layout->addWidget(label);

                auto edit = new QCheckBox(this);
                edit->setFixedSize(input_size);
                layout->addWidget(edit);
                layout->addStretch();
                segment_layout->addSpacing(5);
                segment_layout->addLayout(layout);
                edit->setChecked(security_settings_.get().IsBeingOperatedEnabled());
                const auto settings = security_settings_;
                connect(edit, &QCheckBox::checkStateChanged, this, [settings](Qt::CheckState state) {
                    settings.get().SetCanBeOperated(state == Qt::CheckState::Checked);
                });
            }

            // File Transfer
            {
                auto layout = new NoMarginHLayout();
                auto label = new TcLabel(this);
                label->SetTextId("id_allowed_file_transfer");
                label->setFixedSize(tips_label_size);
                label->setStyleSheet("font-size: 14px; font-weight: 500;");
                layout->addWidget(label);

                auto edit = new QCheckBox(this);
                edit->setFixedSize(input_size);
                layout->addWidget(edit);
                layout->addStretch();
                segment_layout->addSpacing(5);
                segment_layout->addLayout(layout);
                edit->setChecked(security_settings_.get().IsFileTransferEnabled());
                const auto settings = security_settings_;
                connect(edit, &QCheckBox::checkStateChanged, this, [settings](Qt::CheckState state) {
                    settings.get().SetFileTransferEnabled(state == Qt::CheckState::Checked);
                });
            }

            // SSL Always Enabled
            {
                auto layout = new NoMarginHLayout();
                auto label = new TcLabel(this);
                label->SetTextId("id_ssl_enabled");
                label->setFixedSize(tips_label_size);
                label->setStyleSheet("font-size: 14px; font-weight: 500;");
                layout->addWidget(label);

                auto edit = new QCheckBox(this);
                edit->setFixedSize(input_size);
                layout->addWidget(edit);
                layout->addStretch();
                segment_layout->addSpacing(5);
                segment_layout->addLayout(layout);
                edit->setChecked(security_settings_.get().IsSSLConnectionEnabled());

                const auto context = context_;
                const auto settings = security_settings_;
                QPointer<QCheckBox> edit_guard(edit);
                connect(edit, &QCheckBox::stateChanged, this,
                        [context, edit_guard, settings](int state) {
                    const bool enabled = state == 2;
                    if (!enabled) {
                        context->PostUIDelayTask(
                            MakeQtLifetimeAction(edit_guard,
                                [settings](const QPointer<QCheckBox>& checkbox) {
                                    TcDialog dialog(tcTr("id_tips"), tcTr("id_dialog_ssl_always_on"));
                                    dialog.exec();
                                    checkbox->setChecked(settings.get().IsSSLConnectionEnabled());
                                }),
                            50);
                    }
                });
            }

            // record visitor
            {
                auto layout = new NoMarginHLayout();
                auto label = new TcLabel(this);
                label->SetTextId("id_record_visitor");
                label->setFixedSize(tips_label_size);
                label->setStyleSheet("font-size: 14px; font-weight: 500;");
                layout->addWidget(label);

                auto edit = new QCheckBox(this);
                edit->setFixedSize(input_size);
                layout->addWidget(edit);
                layout->addStretch();
                segment_layout->addSpacing(5);
                segment_layout->addLayout(layout);
                edit->setChecked(security_settings_.get().IsVisitHistoryEnabled());

                const auto context = context_;
                const auto settings = security_settings_;
                QPointer<QCheckBox> edit_guard(edit);
                connect(edit, &QCheckBox::stateChanged, this,
                        [context, edit_guard, settings](int state) {
                    const bool enabled = state == 2;
                    if (!enabled) {
                        context->PostUIDelayTask(
                            MakeQtLifetimeAction(edit_guard,
                                [settings](const QPointer<QCheckBox>& checkbox) {
                                    TcDialog dialog(tcTr("id_tips"), tcTr("id_dialog_record_visitor_always_on"));
                                    dialog.exec();
                                    checkbox->setChecked(settings.get().IsVisitHistoryEnabled());
                                }),
                            50);
                    }
                });
            }

            // record file transfer
            {
                auto layout = new NoMarginHLayout();
                auto label = new TcLabel(this);
                label->SetTextId("id_record_file_transfer");
                label->setFixedSize(tips_label_size);
                label->setStyleSheet("font-size: 14px; font-weight: 500;");
                layout->addWidget(label);

                auto edit = new QCheckBox(this);
                edit->setFixedSize(input_size);
                layout->addWidget(edit);
                layout->addStretch();
                segment_layout->addSpacing(5);
                segment_layout->addLayout(layout);
                edit->setChecked(security_settings_.get().IsFileTransferHistoryEnabled());

                const auto context = context_;
                const auto settings = security_settings_;
                QPointer<QCheckBox> edit_guard(edit);
                connect(edit, &QCheckBox::stateChanged, this,
                        [context, edit_guard, settings](int state) {
                    const bool enabled = state == 2;
                    if (!enabled) {
                        context->PostUIDelayTask(
                            MakeQtLifetimeAction(edit_guard,
                                [settings](const QPointer<QCheckBox>& checkbox) {
                                    TcDialog dialog(tcTr("id_tips"), tcTr("id_dialog_record_file_transfer_always_on"));
                                    dialog.exec();
                                    checkbox->setChecked(settings.get().IsFileTransferHistoryEnabled());
                                }),
                            50);
                    }
                });
            }

            // disconnected auto lock screen
            {
                auto layout = new NoMarginHLayout();
                auto label = new TcLabel(this);
                label->SetTextId("id_disconnect_auto_lock_screen");
                label->setFixedSize(tips_label_size);
                label->setStyleSheet("font-size: 14px; font-weight: 500;");
                layout->addWidget(label);

                auto edit = new QCheckBox(this);
                edit->setFixedSize(input_size);
                layout->addWidget(edit);
                layout->addStretch();
                segment_layout->addSpacing(5);
                segment_layout->addLayout(layout);
                edit->setChecked(security_settings_.get().IsDisconnectAutoLockScreenEnabled());

                const auto settings = security_settings_;
                connect(edit, &QCheckBox::checkStateChanged, this, [settings](Qt::CheckState state) {
                    settings.get().SetDisconnectAutoLockScreen(state == Qt::CheckState::Checked);
                });
            }

            // clear all data
            {
                auto layout = new NoMarginHLayout();
                auto label = new TcLabel(this);
                label->SetTextId("id_clear_data");
                label->setFixedSize(tips_label_size);
                label->setStyleSheet("font-size: 14px; font-weight: 500;");
                layout->addWidget(label);

                auto edit = new TcPushButton(this);
                edit->setProperty("class", "danger");
                edit->SetTextId("id_clear");
                edit->setFixedSize(QSize(80, 30));
                edit->setEnabled(true);
                layout->addWidget(edit, 0, Qt::AlignVCenter);
                layout->addStretch();
                segment_layout->addSpacing(5);
                segment_layout->addLayout(layout);
                const auto context = context_;
                const auto settings = security_settings_;
                const auto user_manager = app_->GetUserManager();
                const auto db_manager = context_->GetStreamDBManager();
                connect(edit, &QPushButton::clicked, this,
                        MakeQtLifetimeAction(QPointer<StSecurity>(this),
                            [context, db_manager, settings, user_manager](
                                const QPointer<StSecurity>& self) {
                                TcDialog dialog(tcTr("id_clear"), tcTr("id_ask_clear_data"), self);
                                if (dialog.exec() == kDoneOk) {
                                    // clear
                                    settings.get().ClearData();
                                    user_manager->Clear();
                                    db_manager->Clear();
                                    context->SendAppMessage(MsgForceClearProgramData{});
                                }
                            }));
            }

            ///
            {
                // title
                auto label = new TcLabel(this);
                label->SetTextId("id_application");
                label->setStyleSheet("font-size: 16px; font-weight: 700;");
                segment_layout->addSpacing(20);
                segment_layout->addWidget(label);
            }

            // develop mode
            {
                auto layout = new NoMarginHLayout();
                auto label = new TcLabel(this);
                label->SetTextId("id_developer_mode");
                label->setFixedSize(tips_label_size);
                label->setStyleSheet("font-size: 14px; font-weight: 500;");
                layout->addWidget(label);

                auto edit = new QCheckBox(this);
                edit->setFixedSize(input_size);
                layout->addWidget(edit);
                layout->addStretch();
                segment_layout->addSpacing(5);
                segment_layout->addLayout(layout);
                edit->setChecked(security_settings_.get().IsDevelopMode());
                const auto context = context_;
                const auto settings = security_settings_;
                connect(edit, &QCheckBox::checkStateChanged, this,
                        [context, settings](Qt::CheckState state) {
                    const auto enabled = state == Qt::CheckState::Checked;
                    settings.get().SetDevelopModeEnabled(enabled);
                    context->SendAppMessage(MsgDevelopModeUpdated {
                        .enabled_ = enabled,
                    });
                });
            }

            // Collect logs
            {
                auto layout = new NoMarginHLayout();
                auto label = new TcLabel(this);
                label->SetTextId("id_collect_logs");
                label->setFixedSize(tips_label_size);
                label->setStyleSheet("font-size: 14px; font-weight: 500;");
                layout->addWidget(label);

                auto edit = new TcPushButton(this);
                //edit->setProperty("class", "danger");
                edit->SetTextId("id_collect");
                edit->setFixedSize(QSize(80, 30));
                edit->setEnabled(true);
                layout->addWidget(edit, 0, Qt::AlignVCenter);
                layout->addStretch();
                segment_layout->addSpacing(5);
                segment_layout->addLayout(layout);
                connect(edit, &QPushButton::clicked, this,
                        MakeQtLifetimeAction(QPointer<StSecurity>(this),
                            [](const QPointer<StSecurity>& self) {
                                auto desktop_path = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
                                auto target_dir = TcDialogUtil::SelectDirectory(tcTr("id_save_path"), desktop_path, nullptr);
                                LOGI("Select target dir: {}", target_dir.toStdString());
                                if (target_dir.isEmpty()) {
                                    return;
                                }
                                target_dir += "/px_dat_logs.zip";

                                auto from = FolderUtil::GetProgramDataPath();
                                auto to = from + L"/../back";

                                // delete old backup files
                                FolderUtil::DeleteDir(std::filesystem::path(to));

                                const auto context = self->context_;
                                auto dialog = std::make_shared<InfiniteLoading>(context, tcTr("id_collecting"));
                                dialog->show();
                                auto fn_close_dialog = [context, dialog]() {
                                    context->PostUITask([dialog]() {
                                        dialog->Close();
                                    });
                                };

                                context->PostTask([from, to, target_dir, fn_close_dialog]() {
                                    const std::vector<std::string> ignore_suffix = {
                                        "h264", "h265", "jpg", "png"
                                    };
                                    if (!FolderUtil::CopyDir(from, to, [ignore_suffix](
                                        const std::string&, const std::string& filename) {
                                            // suffix filter
                                            auto suffix = FileUtil::GetFileSuffix(filename);
                                            suffix = StringUtil::ToLowerCpy(suffix);
                                            bool need_ignore_it = false;
                                            for (const auto& sf : ignore_suffix) {
                                                if (suffix.find(sf) != std::string::npos) {
                                                    need_ignore_it = true;
                                                    break;
                                                }
                                            }
                                            if (need_ignore_it) {
                                                return true;
                                            }

                                            // dump filter
                                            if (suffix.find("dmp") != std::string::npos) {
                                                need_ignore_it = true;
                                                LOGI("===> dump file: {}", filename);
                                                if (filename.find("px_") != std::string::npos) {
                                                    need_ignore_it = false;
                                                }
                                            }

                                            return need_ignore_it;
                                    }, true)) {
                                        LOGE("CopyDirectory failed: {} -> {}", QString::fromStdWString(from).toStdString(), QString::fromStdWString(to).toStdString());
                                        fn_close_dialog();
                                        return;
                                    }

                                    const auto& zip_folder = to;
                                    const auto target_zip_file = target_dir.toStdWString();
                                    LOGE("Zip folder: {} -> {}", QString::fromStdWString(zip_folder).toStdString(), QString::fromStdWString(target_zip_file).toStdString());
                                    if (!ZipUtil::ZipFolder(zip_folder, target_zip_file)) {
                                        fn_close_dialog();
                                        LOGE("Zip folder failed!");
                                        return;
                                    }
                                    fn_close_dialog();
                                    FileUtil::SelectFileInExplorer(std::filesystem::path(target_dir.toStdWString()));
                                });
                            }));
            }

            // Exit All Programs
            {
                auto layout = new NoMarginHLayout();
                auto label = new TcLabel(this);
                label->SetTextId("id_exit_all_programs");
                label->setFixedSize(tips_label_size);
                label->setStyleSheet("font-size: 14px; font-weight: 500;");
                layout->addWidget(label);

                auto edit = new TcPushButton(this);
                edit->setProperty("class", "danger");
                edit->SetTextId("id_exit");
                edit->setFixedSize(QSize(80, 30));
                edit->setEnabled(true);
                layout->addWidget(edit, 0, Qt::AlignVCenter);
                layout->addStretch();
                segment_layout->addSpacing(5);
                segment_layout->addLayout(layout);
                const auto context = context_;
                connect(edit, &QPushButton::clicked, this, [context]() {
                    context->SendAppMessage(MsgForceStopAllPrograms{
                        .uninstall_service_ = false,
                    });
                });
            }

            // Uninstall Programs
            {
                auto layout = new NoMarginHLayout();
                auto label = new TcLabel(this);
                label->SetTextId("id_uninstall_all_programs");
                label->setFixedSize(tips_label_size);
                label->setStyleSheet("font-size: 14px; font-weight: 500;");
                layout->addWidget(label);

                auto edit = new TcPushButton(this);
                edit->setProperty("class", "danger");
                edit->SetTextId("id_uninstall");
                edit->setFixedSize(QSize(80, 30));
                edit->setEnabled(true);
                layout->addWidget(edit, 0, Qt::AlignVCenter);
                layout->addStretch();
                segment_layout->addSpacing(5);
                segment_layout->addLayout(layout);
                const auto context = context_;
                connect(edit, &QPushButton::clicked, this, [context]() {
                    context->SendAppMessage(MsgForceStopAllPrograms{
                        .uninstall_service_ = true,
                    });
                });
            }

            column1_layout->addLayout(segment_layout);
        }
        column1_layout->addStretch();

        setLayout(root_layout);
    }

    void StSecurity::OnTabShow() {

    }

    void StSecurity::OnTabHide() {

    }

}
