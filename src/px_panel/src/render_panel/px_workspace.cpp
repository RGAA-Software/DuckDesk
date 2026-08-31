//
// Created by RGAA on 2024/4/9.
//

#include "px_workspace.h"
#include "panel_shutdown_sequence.h"
#include "px_exe_names.h"
#include "px_application.h"
#include "px_common_new/privacy_log.h"
#include "px_common_new/weak_callback.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QApplication>
#include <QMenu>
#include <dwmapi.h>
#include <QSvgRenderer>
#include <QPainter>
#include <QStandardPaths>
#include <QPointer>
#include <QScreen>
#include <QEventLoop>
#include <QDateTime>
#include <utility>

#include "px_qt_widget/custom_tab_btn.h"
#include "px_qt_widget/widget_helper.h"
#include "render_panel/ui/tab_game.h"
#include "render_panel/ui/tab_server.h"
#include "render_panel/ui/tab_cloud_apps.h"
#include "render_panel/ui/tab_settings.h"
#include "render_panel/ui/tab_profile.h"
#include "render_panel/ui/tab_security_internals.h"
#include "render_panel/ui/tab_hw_info.h"
#include "px_settings.h"
#include "px_context.h"
#include "px_render_controller.h"
#include "px_app_messages.h"
#include "service/service_manager.h"
#include "app_colors.h"
#include "render_panel/ui/tab_server_status.h"
#include "px_qt_widget/widgetframe/mainwindow_wrapper.h"
#include "px_qt_widget/widgetframe/titlebar_messages.h"
#include "px_qt_widget/px_dialog.h"
#include "version_config.h"
#include "px_label.h"
#include "no_margin_layout.h"
#include "ui/user/user_login_dialog.h"
#include "px_console_client/console_user_api.h"
#include "ui/tab_cophone.h"
#include "skin/interface/skin_interface.h"
#include "user/px_user_manager.h"
#include "px_common_new/file.h"
#include "px_common_new/file_util.h"
#include "px_common_new/http_client.h"
#include "px_common_new/dump_helper.h"
#include "px_common_new/win32/process_helper.h"
#include "px_qt_widget/px_dialog_util.h"
#include "px_qt_widget/round_img_display.h"
#include "px_qt_widget/image_cropper/image_cropper_dialog.h"
#include "render_panel/ui/user/modify_username_dialog.h"
#include "render_panel/ui/user/modify_password_dialog.h"
#include "render_panel/companion/panel_companion.h"
#include "render_panel/upgrade/upgrade_helper.h"
#include "render_panel/ui/voice_call_consent_dialog.h"
#include "render_panel/network/ws_panel_server.h"
#include "px_render_panel_message.pb.h"
#include "px_message_new/rp_proto_converter.h"

namespace px
{
    std::shared_ptr<PxWorkspace> grWorkspace;

    PxWorkspace::PxWorkspace(bool run_automatically, const std::string& skin_name) : QMainWindow(nullptr) {
        this->run_automatically_ = run_automatically;
        this->skin_name_ = skin_name;
        settings_ = PxSettings::Instance();
        //setWindowFlags(windowFlags() | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
        WidgetHelper::SetTitleBarColor(this);

        auto menu = new QMenu(this);
        sys_tray_icon_ = new QSystemTrayIcon(this);
        sys_tray_icon_->setIcon(QIcon(":/resources/px_icon.png"));
        sys_tray_icon_->setToolTip(tr("Pixels"));

        auto ac_show = new QAction(tcTr("id_show_panel"), this);
        auto ac_exit = new QAction(tcTr("id_exit_all_programs"), this);

        connect(ac_show, &QAction::triggered, this, [=, this](bool) {
            this->showNormal();
        });

        connect(ac_exit, &QAction::triggered, this, [=, this](bool) {
            this->ForceStopAllPrograms(false);
        });

        menu->addAction(ac_show);
        menu->addAction(ac_exit);
        sys_tray_icon_->setContextMenu(menu);
        sys_tray_icon_->show();
        connect(sys_tray_icon_, &QSystemTrayIcon::activated, this, [=, this](QSystemTrayIcon::ActivationReason reason) {
            if (QSystemTrayIcon::ActivationReason::DoubleClick == reason || QSystemTrayIcon::ActivationReason::Trigger == reason) {
#ifdef WIN32
                auto hwnd = (HWND)this->winId();
                ShowWindow(hwnd, SW_RESTORE);
#endif
                this->setWindowState((this->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
                this->show();
                this->raise();
                this->activateWindow();
#ifdef WIN32
                auto fore = GetForegroundWindow();
                if (fore) {
                    auto fore_tid = GetWindowThreadProcessId(fore, nullptr);
                    auto cur_tid = GetCurrentThreadId();
                    AttachThreadInput(cur_tid, fore_tid, TRUE);
                    SetForegroundWindow(hwnd);
                    BringWindowToTop(hwnd);
                    AttachThreadInput(cur_tid, fore_tid, FALSE);
                } else {
                    SetForegroundWindow(hwnd);
                    BringWindowToTop(hwnd);
                }
#endif
            }
        });

        theme_ = new MainWindowPrivate(this);
        QString app_dir = qApp->applicationDirPath();
        QString style_dir = app_dir + "/resources/";
        theme_->AdvancedStyleSheet = new acss::QtAdvancedStylesheet(this);
        theme_->AdvancedStyleSheet->setStylesDirPath(style_dir);
        theme_->AdvancedStyleSheet->setOutputDirPath(app_dir + "/output");
        theme_->AdvancedStyleSheet->setCurrentStyle("qt_material");
        theme_->AdvancedStyleSheet->setCurrentTheme("light_blue");
        theme_->AdvancedStyleSheet->updateStylesheet();
        setWindowIcon(theme_->AdvancedStyleSheet->styleIcon());
        qApp->setStyleSheet(theme_->AdvancedStyleSheet->styleSheet());

        app_ = PxApplication::Make(this, run_automatically, skin_name_);
        context_ = app_->GetContext();
        skin_ = grApp->GetSkin();

        user_mgr_ = grApp->GetUserManager();

        std::string version;
// #if PREMIUM_VERSION
//         if (skin_) {
//             version = skin_->GetAppVersionMode().toStdString();
//         }
//         if (version == "Premium") {
//
//         }
//         else {
//             version = "Premium";
//         }
// #else
//         version = "Freemium";
// #endif

#ifdef OPENSOURCE_BUILD
        version = tcTr("id_version_opensource").toStdString();
#elif defined(OFFICIAL_BUILD)
        version = tcTr("id_version_premium").toStdString();
#else
        version = tcTr("id_version_premium").toStdString();
#endif

        if (skin_) {
            setWindowTitle(std::format("{}(V{} {})", skin_->GetAppName().toStdString(),
                                       skin_->GetAppVersionName().toStdString(),
                                       version).c_str());
        }
        else {
            setWindowTitle(std::format("Pixels(V{} {})", PROJECT_VERSION, version).c_str());
        }

        qApp->installNativeEventFilter(app_.get());

        // window
        auto notifier = app_->GetMessageNotifier();

        // background
        setStyleSheet(R"(QMainWindow {background-color:#FFFFFF;})");

        // root
        auto root_layout = new QHBoxLayout();
        WidgetHelper::ClearMargins(root_layout);

        // left buttons
        {
            auto layout = new QVBoxLayout();
            WidgetHelper::ClearMargins(layout);

            // placeholder to extend the width of left area
            int left_area_width = 220;
            auto extend = new QLabel(this);
            extend->setFixedSize(left_area_width, 2);
            layout->addWidget(extend);

            // logo
            {
                // logo
                auto logo_layout = new NoMarginHLayout();
                auto logo = new RoundImageDisplay("", avatar_size_, avatar_size_, avatar_size_/2);
                logo->SetBorder(2, 0x555555);
                lbl_avatar_ = logo;
                logo->setFixedSize(avatar_size_, avatar_size_);
                logo->setScaledContents(true);
                auto pixmap = WidgetHelper::RenderSvgToPixmap(":/resources/image/ic_not_login.svg", QSize(avatar_size_, avatar_size_));
                logo->UpdatePixmap(pixmap);
                logo_layout->addSpacing(20);
                logo_layout->addWidget(logo);
                logo->SetOnClickListener([=, this](QWidget* w) {
                    if (user_mgr_->IsLoggedIn()) {
                        ShowUserActions();
                    }
                    else {
                        this->ShowUserLoginDialog();
                    }
                });
                logo_layout->addSpacing(8);

                // name
                auto name_layout = new NoMarginVLayout();
                name_layout->addStretch();
                auto lbl = new TcLabel(this);
                lbl_username_ = lbl;
                lbl->setMaximumWidth(125);
                lbl->setStyleSheet("font-weight: 700; color: #333333; font-size: 15px;");

                UpdateUsername();

                lbl->SetOnClickListener([=, this](QWidget* w) {
                    if (user_mgr_->IsLoggedIn()) {
                        ShowUserActions();
                    }
                    else {
                        this->ShowUserLoginDialog();
                    }
                });
                name_layout->addWidget(lbl);

                name_layout->addSpacing(3);

                auto lbl_version = new TcLabel(this);

                lbl_version->setStyleSheet("font-weight: 700; color: #2979ff; font-size: 12px;");
                lbl_version->setText(version.c_str());
                name_layout->addWidget(lbl_version);

                name_layout->addStretch();
                logo_layout->addLayout(name_layout);
                logo_layout->addStretch();
                //

                layout->addSpacing(45);
                layout->addLayout(logo_layout);
            }

            // buttons
            auto btn_font_color = "#ffffff";
            auto btn_size = QSize(left_area_width - 30, 40);
            // remote control
            {
                auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
                btn->AddIcon(":/resources/image/ic_stream_selected.svg", ":/resources/image/ic_stream_normal.svg", 20, 20);
                btn_tab_server_ = btn;
                btn->SetBorderRadius(btn_size.height()/2);
                btn->SetTextId("id_tab_remote_control");
                btn->SetSelectedFontColor(btn_font_color);
                btn->setFixedSize(btn_size);
                QObject::connect(btn, &QPushButton::clicked, this, [=, this]() {
                    ChangeTab(TabName::kTabServer);
                });
                layout->addSpacing(30);
                layout->addWidget(btn, 0, Qt::AlignHCenter);
            }

            // Console cloud applications are a first-class resource and must not be
            // mixed into the remote desktop address book.
            {
                auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
                btn->AddIcon(":/resources/image/ic_cloud_app_selected.svg", ":/resources/image/ic_cloud_app_normal.svg", 20, 20);
                btn_tab_cloud_apps_ = btn;
                btn->SetBorderRadius(btn_size.height()/2);
                btn->SetTextId("id_tab_cloud_applications");
                btn->SetSelectedFontColor(btn_font_color);
                btn->setFixedSize(btn_size);
                QObject::connect(btn, &QPushButton::clicked, this, [=, this]() {
                    ChangeTab(TabName::kTabCloudApps);
                });
                layout->addSpacing(10);
                layout->addWidget(btn, 0, Qt::AlignHCenter);
            }

            // server status
            {
                auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
                btn->AddIcon(":/resources/image/ic_statistics_selected.svg", ":/resources/image/ic_statistics_normal.svg", 20, 20);
                btn_tab_server_status_ = btn;
                btn->SetBorderRadius(btn_size.height()/2);
                btn->SetTextId("id_tab_server_status");
                btn->SetSelectedFontColor(btn_font_color);
                btn->setFixedSize(btn_size);
                QObject::connect(btn, &QPushButton::clicked, this, [=, this]() {
                    ChangeTab(TabName::kTabServerStatus);
                });
                layout->addSpacing(10);
                layout->addWidget(btn, 0, Qt::AlignHCenter);
            }

            if (skin_ && skin_->IsGameEnabled()) {
                auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
                btn->AddIcon(":/resources/image/ic_game_selected.svg", ":/resources/image/ic_game_normal.svg", 20, 20);
                btn_tab_games_ = btn;
                btn->SetBorderRadius(btn_size.height()/2);
                btn->SetTextId("id_tab_games");
                btn->SetSelectedFontColor(btn_font_color);
                btn->setFixedSize(btn_size);
                QObject::connect(btn, &QPushButton::clicked, this, [=, this]() {
                    ChangeTab(TabName::kTabGames);
                });
                layout->addSpacing(10);
                layout->addWidget(btn, 0, Qt::AlignHCenter);
            }

            if (skin_ && skin_->IsCoPhoneEnabled()) {
                auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
                btn->AddIcon(":/resources/image/ic_device_selected.svg", ":/resources/image/ic_device_normal.svg", 20, 20);
                btn_tab_cophone_ = btn;
                btn->SetBorderRadius(btn_size.height()/2);
                btn->SetTextId("id_co_phone");
                btn->SetSelectedFontColor(btn_font_color);
                btn->setFixedSize(btn_size);
                QObject::connect(btn, &QPushButton::clicked, this, [=, this]() {
                    ChangeTab(TabName::kTabCoPhone);
                });
                layout->addSpacing(10);
                layout->addWidget(btn, 0, Qt::AlignHCenter);
            }

            {
                auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
                btn->AddIcon(":/resources/image/ic_settings_security_selected.svg", ":/resources/image/ic_settings_security_normal.svg", 20, 20);
                btn_security_ = btn;
                btn->SetBorderRadius(btn_size.height()/2);
                btn->SetTextId("id_tab_security");
                btn->SetSelectedFontColor(btn_font_color);
                btn->setFixedSize(btn_size);
                QObject::connect(btn, &QPushButton::clicked, this, [=, this]() {
                    ChangeTab(TabName::kTabSecurity);
                });
                layout->addSpacing(10);
                layout->addWidget(btn, 0, Qt::AlignHCenter);
            }

            {
                auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
                btn->AddIcon(":/resources/image/ic_settings_outline_selected.svg", ":/resources/image/ic_settings_outline_normal.svg", 20, 20);
                btn_tab_settings_ = btn;
                btn->SetBorderRadius(btn_size.height()/2);
                btn->SetTextId("id_settings");
                btn->SetSelectedFontColor(btn_font_color);
                btn->setFixedSize(btn_size);
                QObject::connect(btn, &QPushButton::clicked, this, [=, this]() {
                    ChangeTab(TabName::kTabSettings);
                });
                layout->addSpacing(10);
                layout->addWidget(btn, 0, Qt::AlignHCenter);
            }

            {
                auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
                btn->AddIcon(":/resources/image/ic_hw_selected.svg", ":/resources/image/ic_hw_normal.svg", 20, 20);
                btn_tab_hw_info_ = btn;
                btn->SetBorderRadius(btn_size.height()/2);
                btn->SetTextId("id_tab_hardware");
                btn->SetSelectedFontColor(btn_font_color);
                btn->setFixedSize(btn_size);
                QObject::connect(btn, &QPushButton::clicked, this, [=, this]() {
                    ChangeTab(TabName::kTabHWInfo);
                });
                layout->addSpacing(10);
                layout->addWidget(btn, 0, Qt::AlignHCenter);
            }

            /// Splitter
            layout->addStretch(100);

            auto exit_btn_size = QSize(btn_size.width(), btn_size.height() - 5);

            // jump to github
            {
                QWidget* w = new QWidget(this);
                jump_to_github_widget_ = w;
                w->setObjectName("jump_github");
                w->setStyleSheet(R"(
                    #jump_github {
                        background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #5578E8, stop:1 #6488E8);
                        border-radius: 5px;
                        padding: 5px;
                    }
                    #jump_github:hover {
                        background: qlineargradient(x1:0, y1:0, x2:1, y2:1, 
                                                    stop:0 #5578E8, stop:1 #6488E8);
                    }
                    #jump_github:pressed {
                        background: qlineargradient(x1:0, y1:0, x2:1, y2:1, 
                                                    stop:0 #5578FE8, stop:1 #6488E8);
                    }
                )");

                w->setFixedSize(btn_size.width(), btn_size.height() * 2.2);
                
                layout->addWidget(w, 0, Qt::AlignHCenter);
                layout->addSpacing(10);

                QVBoxLayout* vlayout = new QVBoxLayout(w);
                vlayout->setSpacing(10);
                vlayout->setContentsMargins(4, 4, 4, 4);

                QLabel* label = new QLabel(w);
                label->setText(tcTr("id_find_new_version"));
                label->setStyleSheet(R"(
                    QLabel {
                        font-size: 16px;
                        color: #ffffff;
                    }
                )");
                vlayout->addWidget(label, 0, Qt::AlignCenter);

                QPushButton* btn = new QPushButton(w);
                btn->setText(tcTr("id_click_to_down"));
                btn->setFixedSize(btn_size.width()-20, btn_size.height() - 6);
                btn->setStyleSheet(R"(
                    QPushButton{
                        color: white;
                        text - decoration: underline;
                        border: none;
                        padding: 8px 16px;
                        font-size: 14px;
                    }
                )");
                btn->setCursor(QCursor(Qt::PointingHandCursor));
                vlayout->addWidget(btn, 0, Qt::AlignCenter);
                w->hide();

                const auto app = app_;
                connect(btn, &QPushButton::clicked, this, [app]() {
                    app->JumpToOffSiteUpdate();
                });
            }
           
            // stop all
            {
                auto btn = new QPushButton(this);
                btn_exit_ = btn;
                btn->setText(tcTr("id_exit_all_programs"));
                btn->setProperty("class", "danger");
                //btn->setProperty("flat", true);
                btn->setFixedSize(exit_btn_size);
                QObject::connect(btn, &QPushButton::clicked, this, [=, this]() {
                    this->ForceStopAllPrograms(false);
                });
                layout->addWidget(btn, 0, Qt::AlignHCenter);
                layout->addSpacing(5);

                btn->setHidden(!settings_->IsDevelopMode());
            }

            // uninstall all
            {
                auto btn = new QPushButton(this);
                btn_uninstall_ = btn;
                btn->setText(tcTr("id_uninstall_all_programs"));
                btn->setProperty("class", "danger");
                //btn->setProperty("flat", true);
                btn->setFixedSize(exit_btn_size);
                QObject::connect(btn, &QPushButton::clicked, this, [=, this]() {
                    this->ForceStopAllPrograms(true);
                });
                layout->addWidget(btn, 0, Qt::AlignHCenter);
                layout->addSpacing(5);

                btn->setHidden(!settings_->IsDevelopMode());
            }

            {
                auto lbl = new QLabel(this);
                lbl->setFixedSize(190, 40);
                if (skin_) {
                    auto p = skin_->GetLargeIconTextLogo();
                    lbl->setPixmap(p);
                }
                layout->addSpacing(8);
                layout->addWidget(lbl, 0, Qt::AlignHCenter);
                layout->addSpacing(8);
            }

            root_layout->addLayout(layout);

        }

        // right panels
        {
            // tabs
            tabs_.insert({TabName::kTabServer, new TabServer(app_, this)});
            tabs_.insert({TabName::kTabCloudApps, new TabCloudApps(app_, this)});
            tabs_.insert({TabName::kTabServerStatus, new TabServerStatus(app_, this)});
            if (skin_ && skin_->IsGameEnabled()) {
                tabs_.insert({TabName::kTabGames, new TabGame(app_, this)});
            }
            if (skin_ && skin_->IsCoPhoneEnabled()) {
                tabs_.insert({TabName::kTabCoPhone, new TabCoPhone(app_, this)});
            }
            tabs_.insert({TabName::kTabSettings, new TabSettings(app_, this)});
            tabs_.insert({TabName::kTabSecurity, new TabSecurityInternals(app_, this)});
            //tabs_.insert({TabName::kTabProfile, new TabProfile(app_, this)});
            tabs_.insert({TabName::kTabHWInfo, new TabHWInfo(app_, this)});

            tabs_[TabName::kTabServer]->SetAttach(btn_tab_server_);
            tabs_[TabName::kTabCloudApps]->SetAttach(btn_tab_cloud_apps_);
            tabs_[TabName::kTabServerStatus]->SetAttach(btn_tab_server_status_);
            if (skin_ && skin_->IsGameEnabled()) {
                tabs_[TabName::kTabGames]->SetAttach(btn_tab_games_);
            }
            if (skin_ && skin_->IsCoPhoneEnabled()) {
                tabs_[TabName::kTabCoPhone]->SetAttach(btn_tab_cophone_);
            }
            tabs_[TabName::kTabSettings]->SetAttach(btn_tab_settings_);
            tabs_[TabName::kTabSecurity]->SetAttach(btn_security_);
            //tabs_[TabName::kTabProfile]->SetAttach(btn_tab_profile_);
            tabs_[TabName::kTabHWInfo]->SetAttach(btn_tab_hw_info_);

            auto layout = new QVBoxLayout();
            WidgetHelper::ClearMargins(root_layout);
            auto stack_widget = new QStackedWidget(this);
            stack_widget->addWidget(tabs_[TabName::kTabServer]);
            stack_widget->addWidget(tabs_[TabName::kTabCloudApps]);
            stack_widget->addWidget(tabs_[TabName::kTabServerStatus]);
            if (skin_ && skin_->IsGameEnabled()) {
                stack_widget->addWidget(tabs_[TabName::kTabGames]);
            }
            if (skin_ && skin_->IsCoPhoneEnabled()) {
                stack_widget->addWidget(tabs_[TabName::kTabCoPhone]);
            }
            stack_widget->addWidget(tabs_[TabName::kTabSettings]);
            stack_widget->addWidget(tabs_[TabName::kTabSecurity]);
            //stack_widget->addWidget(tabs_[TabName::kTabProfile]);
            stack_widget->addWidget(tabs_[TabName::kTabHWInfo]);
            stacked_widget_ = stack_widget;
            layout->addWidget(stack_widget);
            root_layout->addLayout(layout);
        }

        auto root_widget = new QWidget(this);
        root_widget->setLayout(root_layout);
        setCentralWidget(root_widget);

        ChangeTab(TabName::kTabServer);

        // last works
        app_->RequestNewClientId(false);

        //
        if (user_mgr_->IsLoggedIn()) {
            this->Login();
        }

        InitListeners();
    }

    void PxWorkspace::Init() {
        grWorkspace = shared_from_this();
        const auto weak_workspace = weak_from_this();

        QTimer::singleShot(
            2000, this,
            MakeWeakVoidCallback(
                weak_workspace,
                [](const std::shared_ptr<PxWorkspace>& workspace) {
                    workspace->InitUpdate();
                }));

        QTimer::singleShot(
            50, this,
            MakeWeakVoidCallback(
                weak_workspace,
                [](const std::shared_ptr<PxWorkspace>& workspace) {
                    QPointer<QScreen> screen(qApp->primaryScreen());
                    if (!screen) {
                        return;
                    }

                    const auto screen_size = screen->size();
                    const auto x = (screen_size.width() - workspace->size().width()) / 2;
                    const auto y = (screen_size.height() - workspace->size().height() - 48) / 2;
                    workspace->move(x, y);
                }));

        ScheduleOffSiteUpdateCheck(app_, context_, weak_workspace);
    }

    void PxWorkspace::ChangeTab(const TabName& tn) {
        for (auto& [name, tab] : tabs_) {
            if (tn == name) {
                stacked_widget_->setCurrentWidget(tabs_[tn]);
                tabs_[tn]->OnTabShow();
                ((CustomTabBtn*)tabs_[tn]->GetAttach())->ToActiveStatus();
            } else {
                tabs_[name]->OnTabHide();
                ((CustomTabBtn*)tabs_[name]->GetAttach())->ToInActiveStatus();
            }
        }
    }

    void PxWorkspace::closeEvent(QCloseEvent *event) {
        if (upgrade_helper_widget_) {
            upgrade_helper_widget_->done(QDialog::Rejected);
            upgrade_helper_widget_->close();
            upgrade_helper_widget_.reset();
        }
        event->ignore();
        TcDialog dialog(tcTr("id_hide"), tcTr("id_hide_gammaray_msg"), this);
        if (kDoneOk == dialog.exec()) {
            this->hide();
        }
    }

    void PxWorkspace::resizeEvent(QResizeEvent *event) {
        QMainWindow::resizeEvent(event);
    }

    void PxWorkspace::InitListeners() {
        msg_listener_ = context_->ObtainUIMessageListener();
        QPointer<PxWorkspace> self(this);
        msg_listener_->Listen<MsgTitleBarSettingsClicked>([self](const MsgTitleBarSettingsClicked&) {
            if (self) {
                self->ChangeTab(TabName::kTabSettings);
            }
        });

        msg_listener_->Listen<MsgTitleBarAvatarClicked>([](const MsgTitleBarAvatarClicked&) {
        });

        // develop mode update
        msg_listener_->Listen<MsgDevelopModeUpdated>([self](const MsgDevelopModeUpdated& msg) {
            if (self) {
                self->btn_exit_->setHidden(!msg.enabled_);
                self->btn_uninstall_->setHidden(!msg.enabled_);
            }
        });

        // force stop all programs
        msg_listener_->Listen<MsgForceStopAllPrograms>([self](const MsgForceStopAllPrograms& msg) {
            if (self) {
                self->ForceStopAllPrograms(msg.uninstall_service_);
            }
        });

        // clear data
        msg_listener_->Listen<MsgForceClearProgramData>([self](const MsgForceClearProgramData&) {
            if (self) {
                self->ClearUserInfo();
            }
        });

        // check update
        msg_listener_->Listen<MsgCheckUpdate>([self](const MsgCheckUpdate&) {
            if (self) {
                self->CheckAppUpdate(true);
            }
        });

        // update
        msg_listener_->Listen<MsgGrTimer10H>([self, context = context_](const MsgGrTimer10H&) {
            if (!self) {
                return;
            }
            CheckAppUpdate(false);
            context->PostTask([]() {
                px::ClearOldDumps();
            });
            ScheduleOffSiteUpdateCheck(self->app_, context, self->weak_from_this());
        });

        msg_listener_->Listen<MsgPanelVoiceCallConsentRequest>(
            [self, context = context_](const MsgPanelVoiceCallConsentRequest& msg) {
                context->PostUITask([self, msg]() {
                    if (self) {
                        self->ShowVoiceCallConsent(msg);
                    }
                });
            });

        msg_listener_->Listen<MsgPanelVoiceCallConsentCancel>(
            [self, context = context_](const MsgPanelVoiceCallConsentCancel& msg) {
                context->PostUITask([self, msg]() {
                    if (self) {
                        self->CancelVoiceCallConsent(msg);
                    }
                });
            });
    }

    void PxWorkspace::ShowVoiceCallConsent(const MsgPanelVoiceCallConsentRequest& msg) {
        LOGI("[VoiceCall] showing consent dialog, call={}, stream={}, request={}",
             PrivacyLogId(msg.call_id_), msg.stream_id_, msg.request_id_);
        VoiceCallConsentInfo info{
            .visitor_device_id = msg.visitor_device_id_,
            .stream_id = msg.stream_id_,
            .call_id = msg.call_id_,
            .request_id = msg.request_id_,
            .expires_at_unix_ms = msg.expires_at_unix_ms_,
        };
        if (!info.IsValid() || msg.protocol_version_ != 1) {
            SendVoiceCallConsentDecision(info, false, "unsupported");
            return;
        }
        const auto now = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
        if (now >= info.expires_at_unix_ms) {
            SendVoiceCallConsentDecision(info, false, "timeout");
            return;
        }
        if (voice_call_consent_dialog_) {
            if (voice_call_consent_dialog_->Matches(
                    info.stream_id, info.call_id, info.request_id)) {
                return;
            }
            SendVoiceCallConsentDecision(info, false, "busy");
            return;
        }

        const QPointer<PxWorkspace> self(this);
        voice_call_consent_dialog_ =
            new VoiceCallConsentDialog( // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns it; QPointer observes it
            info,
            [self, info](bool accepted, const std::string& reason) {
                if (!self) {
                    return;
                }
                self->voice_call_consent_dialog_ = nullptr;
                self->SendVoiceCallConsentDecision(info, accepted, reason);
            },
            this);
        voice_call_consent_dialog_->ShowProminently();
    }

    void PxWorkspace::CancelVoiceCallConsent(const MsgPanelVoiceCallConsentCancel& msg) {
        if (!voice_call_consent_dialog_ ||
            !voice_call_consent_dialog_->Matches(
                msg.stream_id_, msg.call_id_, msg.request_id_)) {
            return;
        }
        const QPointer<VoiceCallConsentDialog> dialog =
            voice_call_consent_dialog_;
        voice_call_consent_dialog_ = nullptr;
        LOGI("[VoiceCall] closing consent dialog without decision, call={}, stream={}, request={}",
             PrivacyLogId(msg.call_id_), msg.stream_id_, msg.request_id_);
        dialog->CancelWithoutDecision();
    }

    void PxWorkspace::SendVoiceCallConsentDecision(
        const VoiceCallConsentInfo& info, bool accepted,
        const std::string& reason) {
        if (!app_) {
            return;
        }
        const auto server = app_->GetWsPanelServer();
        if (!server) {
            return;
        }
        pxrp::RpMessage message;
        message.set_type(pxrp::kRpVoiceCallConsentDecision);
        auto* decision = message.mutable_voice_call_consent_decision();
        decision->set_stream_id(info.stream_id);
        decision->set_call_id(info.call_id);
        decision->set_request_id(info.request_id);
        decision->set_accepted(accepted);
        decision->set_reason(reason);
        LOGI("[VoiceCall] px_panel sending consent decision, accepted={}, call={}, stream={}, request={}, reason={}",
             accepted, PrivacyLogId(info.call_id), info.stream_id, info.request_id, reason);
        server->PostRendererMessage(RpProtoAsData(&message));
    }

    void PxWorkspace::ForceStopAllPrograms(bool uninstall_service) {
        TcDialog dialog(tcTr("id_exit"), uninstall_service ? tcTr("id_uninstall_gammaray_msg") : tcTr("id_exit_gammaray_msg"), this);
        if (dialog.exec() == kDoneOk) {
            // Exit Programs must close controller windows before taking down
            // Render. Otherwise an active file-only client observes the server
            // disconnect and briefly replaces its window with the reconnect
            // warning while the remaining shutdown steps are still running.
            LOGI("Force close all px_client processes before stopping desktop.");
            px::ProcessHelper::CloseProcessesByName(px::kPxClientExeName);

            // 关键:通过 service WS 发 StopDesktop,清掉持久化的 last_desktop_launch。
            // 否则只杀 Render 进程时,仍在跑的 px_service 监控循环会马上把它拉起来
            // (日志: desktop render missing, restarting)。
            if (auto render_ctrl = app_->GetContext()->GetRenderController()) {
                LOGI("Request service StopDesktop before exit (clear persisted launch).");
                render_ctrl->StopServer();
            }
            // StopServer 走 async_send；延迟关闭 service client，但不阻塞 Qt UI。
            QApplication::processEvents(QEventLoop::AllEvents, 100);
            const auto app = app_;
            const auto context = app_->GetContext();
            const auto srv_mgr = context->GetServiceManager();
            const auto current_pid = px::ProcessHelper::GetCurrentProcessId();
            const QPointer<PxWorkspace> guarded_workspace(this);
            shutdown_sequence_ = PanelShutdownSequence::Make(
                [context](PanelShutdownSequence::Task task, int delay_ms) {
                    context->PostUIDelayTask(std::move(task), delay_ms);
                },
                [context](PanelShutdownSequence::Task task, int delay_ms) {
                    context->PostDelayTask(std::move(task), delay_ms);
                },
                PanelShutdownSequence::Hooks{
                .prepare = [app, guarded_workspace]() {
                    app->PrepareForShutdown();
                    if (guarded_workspace) {
                        guarded_workspace->hide();
                        guarded_workspace->setEnabled(false);
                    }
                },
                .launch_service_helper = [srv_mgr, uninstall_service, current_pid]() {
                    if (!srv_mgr) {
                        return false;
                    }
                    LOGI("ShutdownDetached uninstall={}, panel_pid={}",
                         uninstall_service, current_pid);
                    return srv_mgr->ShutdownDetached(uninstall_service, current_pid);
                },
                .fallback_cleanup = [current_pid]() {
                    px::ProcessHelper::CloseProcessesByName(px::kPxRenderExeName);
                    px::ProcessHelper::CloseProcessesByName(px::kPxFunctionExeName);
                    px::ProcessHelper::CloseProcessesByName(px::kPxOsInfoExeName);
                    LOGI("Force close current px_panel process, pid={}", current_pid);
                    px::ProcessHelper::CloseProcess(current_pid);
                },
            });
            shutdown_sequence_->Start();
        }
    }

    void PxWorkspace::Login() {
        QPointer<PxWorkspace> self(this);
        context_->PostTask([self]() {
            if (!self) {
                return;
            }
            if (!self->user_mgr_->IsLoggedIn()) {
                return;
            }
            self->LoadAvatar();
        });
    }

    void PxWorkspace::ShowUserLoginDialog() {
        UserLoginDialog dialog(app_->GetContext());
        auto r = dialog.exec();
        if (r == 0) {
            UpdateUserInfo();
        }
    }

    void PxWorkspace::ShowSelectAvatarDialog() {
        auto desktop_path = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        auto image_path = TcDialogUtil::SelectImage(tcTr("id_select_image"), desktop_path, nullptr);
        if (image_path.isEmpty()) {
            return;
        }
        QPixmap image = ImageCropperDialog::getCroppedImage(image_path, 600, 400, CropperShape::CIRCLE);
        if (image.isNull()) {
            return;
        }

        auto avatar_path = settings_->GetGrDataCachePath() + "/" + grApp->GetUserManager()->GetUserId() + "_avatar.jpg";
        image.save(avatar_path.c_str());

        if (!File::Exists(U8Path(avatar_path))) {
            LOGE("Crop image failed, file not exists: {}", avatar_path);
            return;
        }

        auto size = File::Size(U8Path(avatar_path));
        if (size < 0 || size >= 10 * 1024 * 1024) {
            LOGE("Image size invalid: {}", size);
            return;
        }

        if (user_mgr_->UpdateAvatar(avatar_path)) {
            LoadAvatar();
        }
    }

    void PxWorkspace::UpdateUserInfo() {
        UpdateUsername();
        LoadAvatar();
    }

    void PxWorkspace::ClearUserInfo() {
        lbl_username_->SetTextId("id_guest");
    }

    void PxWorkspace::UpdateUsername() {
        if (user_mgr_->IsLoggedIn()) {
            lbl_username_->SetTextId("");
            lbl_username_->setText(user_mgr_->GetUsername().c_str());
        }
        else {
            lbl_username_->SetTextId("id_guest");
        }
    }

    void PxWorkspace::ShowUserActions() {
        auto menu = new QMenu();
        std::vector<QString> actions = {
            tcTr("id_edit_username"),
            tcTr("id_edit_password"),
            tcTr("id_edit_avatar"),
            // "",
            // tcTr("id_user_center"),
            "",
            tcTr("id_exit_login"),
        };
        for (int i = 0; i < actions.size(); i++) {
            const QString& action_name = actions.at(i);
            if (action_name.isEmpty()) {
                menu->addSeparator();
                continue;
            }

            auto action = new QAction(action_name, menu);
            menu->addAction(action);
            connect(action, &QAction::triggered, this, [=, this]() {
                ProcessUserAction(i);
            });
        }
        menu->exec(QCursor::pos());
        delete menu;
    }

    void PxWorkspace::ProcessUserAction(int index) {
        // modify username
        if (index == 0) {
            ModifyUsernameDialog dialog(context_);
            if (dialog.exec() == kDoneOk) {
                this->UpdateUsername();
            }
        }
        else if (index == 1) {
            // modify password
            ModifyPasswordDialog dialog(context_);
            if (dialog.exec() == kDoneOk) {

            }
        }
        else if (index == 2) {
            // update avatar
            ShowSelectAvatarDialog();
        }
        // else if (index == 4) {
        //     // user center
        // }
        else if (index == 4) {
            // exit
            TcDialog dialog(tcTr("id_exit_login"), tcTr("id_exit_login_msg"));
            if (dialog.exec() == kDoneOk) {
                // logout
                user_mgr_->Logout();

                // clear avatar
                ClearAvatar();

                // clear database
                user_mgr_->Clear();

                // clear ui
                ClearUserInfo();
                // avatar

                // send a logged in message
                context_->SendAppMessage(MsgUserLoggedOut {});
            }
        }
    }

    void PxWorkspace::LoadAvatar() {
        QPointer<PxWorkspace> self(this);
        context_->PostTask([self]() {
            if (!self) {
                return;
            }
            auto avatar_path = self->user_mgr_->GetAvatarPath();
            if (avatar_path.starts_with("./")) {
                avatar_path = avatar_path.substr(1);
            }
            auto avatar_url_path = std::format("{}://{}:{}{}?appkey={}", PxSettings::GetConsoleHttpScheme(), self->settings_->GetConsoleServerHost(), self->settings_->GetConsoleServerPort(), avatar_path, grApp->GetAppkey());
            auto target_avatar_path = self->settings_->GetGrDataCachePath() + "/" + self->user_mgr_->GetUserId() + + "." + FileUtil::GetFileSuffix(avatar_path);
            LOGI("Cached avatar path: {}", target_avatar_path);
            if (File::Exists(U8Path(target_avatar_path))) {
                LOGI("Load local avatar first");
                self->context_->PostUITask([self, target_avatar_path]() {
                    if (!self) {
                        return;
                    }
                    self->SetAvatar(target_avatar_path);
                });
            }

            auto target_avatar_cache_path = self->settings_->GetGrDataCachePath() + "/" + self->user_mgr_->GetUserId() + + "_cache." + FileUtil::GetFileSuffix(avatar_path);
            auto file = File::OpenForWriteB(U8Path(target_avatar_cache_path));
            auto r = HttpClient::Download(avatar_url_path, [file](const std::string& d) {
                file->Append(d);
            });
            if (r.status == 200) {
                LOGI("Load avatar from server and refresh it!");
                file->Close();
                File::Delete(U8Path(target_avatar_path));
                FileUtil::ReName(PathFromUTF8(target_avatar_cache_path), PathFromUTF8(target_avatar_path));
                File::Delete(U8Path(target_avatar_cache_path));
                self->context_->PostUITask([self, target_avatar_path]() {
                    if (!self) {
                        return;
                    }
                    self->SetAvatar(target_avatar_path);
                });
            }
            else {
                file->Close();
            }
        });
    }

    void PxWorkspace::SetAvatar(const std::string& filepath) {
        auto pixmap = QPixmap::fromImage(QImage(filepath.c_str()));
        if (pixmap.isNull()) {
            return;
        }
        pixmap = pixmap.scaled(avatar_size_, avatar_size_, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        lbl_avatar_->UpdatePixmap(pixmap);
    }

    void PxWorkspace::ClearAvatar() {
        auto pixmap = WidgetHelper::RenderSvgToPixmap(":/resources/image/ic_not_login.svg", QSize(avatar_size_, avatar_size_));
        lbl_avatar_->UpdatePixmap(pixmap);
    }


    void PxWorkspace::InitUpdate() {
        if (update_callbacks_registered_) {
            return;
        }
        update_callbacks_registered_ = true;

        const auto weak_workspace = weak_from_this();
        auto& update_manager = UpdateManager::Instance();
        QObject::connect(
            &update_manager, &UpdateManager::SigFindUpdate, this,
            MakeWeakVoidCallback(
                weak_workspace,
                [](const std::shared_ptr<PxWorkspace>& workspace,
                   const QVariantMap& data) {
                    workspace->showNormal();

                    if (workspace->upgrade_helper_widget_) {
                        workspace->upgrade_helper_widget_->done(QDialog::Rejected);
                        workspace->upgrade_helper_widget_->close();
                        workspace->upgrade_helper_widget_.reset();
                    }

                    const auto dialog = std::make_shared<UpgradeHelperWidget>();
                    workspace->upgrade_helper_widget_ = dialog;
                    dialog->SetRemoteVersion(data["version"].toString());
                    dialog->SetRemoteUpdateDesc(data["desc"].toString());
                    dialog->SetForced(data["forced"].toBool());
                    dialog->raise();
                    dialog->exec();
                    if (dialog->exit_app_) {
                        workspace->close();
                    }
                    if (workspace->upgrade_helper_widget_ == dialog) {
                        workspace->upgrade_helper_widget_.reset();
                    }
                }));

        QObject::connect(
            &update_manager, &UpdateManager::SigUpdateHint, this,
            MakeWeakVoidCallback(
                weak_workspace,
                [](const std::shared_ptr<PxWorkspace>& workspace, const QString& info) {
                    workspace->showNormal();
                    TcDialog dialog(tcTr("id_tips"), info, workspace.get());
                    dialog.exec();
                }));

        update_manager.CheckUpdate(true, false);
    }

    void PxWorkspace::CheckAppUpdate(bool from_user_clicked) {
        UpdateManager::Instance().CheckUpdate(true, from_user_clicked);
    }

    void PxWorkspace::ScheduleOffSiteUpdateCheck(
        const std::shared_ptr<PxApplication>& app,
        const std::shared_ptr<PxContext>& context,
        std::weak_ptr<PxWorkspace> weak_workspace) {
        context->PostTask([app, context, weak_workspace = std::move(weak_workspace)]() {
            const bool has_update = app->HasOffSiteUpdate();
            context->PostUITask(MakeWeakVoidCallback(
                weak_workspace,
                [has_update](const std::shared_ptr<PxWorkspace>& workspace) {
                    if (workspace->jump_to_github_widget_) {
                        workspace->jump_to_github_widget_->setVisible(has_update);
                    }
                }));
        });
    }
}
