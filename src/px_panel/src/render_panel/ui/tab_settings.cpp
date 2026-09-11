//
// Created by RGAA on 2024-04-09.
//

#include "tab_settings.h"
#include "px_qt_widget/custom_tab_btn.h"
#include "px_qt_widget/no_margin_layout.h"
#include "render_panel/ui/st_general.h"
#include "render_panel/ui/st_about_me.h"
#include "render_panel/ui/st_network.h"
#include "render_panel/ui/st_controller.h"
#include "app_colors.h"
#include "st_security.h"

namespace px
{

    TabSettings::TabSettings(const std::shared_ptr<PxApplication>& app, QWidget* parent) : TabBase(app, parent) {
        auto root_layout = new NoMarginHLayout();

        auto left_button_layout = new NoMarginVLayout();
        // title margin
        left_button_layout->addSpacing(kTabContentMarginTop);

        root_layout->addSpacing(20);
        root_layout->addLayout(left_button_layout);
        auto left_area_width = 180;
        auto btn_size = QSize(left_area_width - 30, 32);
        auto btn_font_color = "#ffffff";
        int border_radius = btn_size.height()/2;
        // General
        {
            auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
            btn_input_ = btn;
            btn->SetBorderRadius(border_radius);
            btn->SetTextId("id_settings_general");

            btn->SetSelectedFontColor(btn_font_color);
            btn->setFixedSize(btn_size);
            //tab_btns.insert(std::make_pair(TabType::kInstalled, btn));
            const QPointer<TabSettings> self(this);
            QObject::connect(btn, &QPushButton::clicked, this, [self]() {
                if (self) {
                    self->ChangeTab(StTabName::kStGeneral);
                }
            });
            //left_button_layout->addSpacing(30);
            left_button_layout->addWidget(btn, 0, Qt::AlignHCenter);
        }
        // network
        {
            auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
            btn_network_ = btn;
            btn->SetBorderRadius(border_radius);
            btn->SetTextId("id_settings_network");

            btn->SetSelectedFontColor(btn_font_color);
            btn->setFixedSize(btn_size);
            //tab_btns.insert(std::make_pair(TabType::kInstalled, btn));
            const QPointer<TabSettings> self(this);
            QObject::connect(btn, &QPushButton::clicked, this, [self]() {
                if (self) {
                    self->ChangeTab(StTabName::kStNetwork);
                }
            });
            left_button_layout->addSpacing(10);
            left_button_layout->addWidget(btn, 0, Qt::AlignHCenter);
        }
        // security
        {
            auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
            btn_security_ = btn;
            btn->SetBorderRadius(border_radius);
            btn->SetTextId("id_settings_security");

            btn->SetSelectedFontColor(btn_font_color);
            btn->setFixedSize(btn_size);
            //tab_btns.insert(std::make_pair(TabType::kInstalled, btn));
            const QPointer<TabSettings> self(this);
            QObject::connect(btn, &QPushButton::clicked, this, [self]() {
                if (self) {
                    self->ChangeTab(StTabName::kStSecurity);
                }
            });
            left_button_layout->addSpacing(10);
            left_button_layout->addWidget(btn, 0, Qt::AlignHCenter);
        }
        // controller
        {
            auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
            btn_controller_ = btn;
            btn->SetBorderRadius(border_radius);
            btn->SetTextId("id_settings_controller");

            btn->SetSelectedFontColor(btn_font_color);
            btn->setFixedSize(btn_size);
            //tab_btns.insert(std::make_pair(TabType::kInstalled, btn));
            const QPointer<TabSettings> self(this);
            QObject::connect(btn, &QPushButton::clicked, this, [self]() {
                if (self) {
                    self->ChangeTab(StTabName::kStController);
                }
            });
            left_button_layout->addSpacing(10);
            left_button_layout->addWidget(btn, 0, Qt::AlignHCenter);
        }
        // About me
        {
            auto btn = new CustomTabBtn(AppColors::kTabBtnInActiveColor, AppColors::kTabBtnHoverColor, this);
            btn_about_me_ = btn;
            btn->SetBorderRadius(border_radius);
            btn->SetTextId("id_settings_aboutme");

            btn->SetSelectedFontColor(btn_font_color);
            btn->setFixedSize(btn_size);
            //tab_btns.insert(std::make_pair(TabType::kInstalled, btn));
            const QPointer<TabSettings> self(this);
            QObject::connect(btn, &QPushButton::clicked, this, [self]() {
                if (self) {
                    self->ChangeTab(StTabName::kStAboutMe);
                }
            });
            left_button_layout->addSpacing(10);
            left_button_layout->addWidget(btn, 0, Qt::AlignHCenter);
        }
        left_button_layout->addStretch();

        {
            // tabs
            tabs_.insert({StTabName::kStGeneral, new StGeneral(app_, this)});
            tabs_.insert({StTabName::kStNetwork, new StNetwork(app_, this)});
            tabs_.insert({StTabName::kStSecurity, new StSecurity(app_, this)});
            tabs_.insert({StTabName::kStController, new StController(app_, this)});
            tabs_.insert({StTabName::kStAboutMe, new StAboutMe(app_, this)});

            tabs_[StTabName::kStGeneral]->SetAttach(btn_input_);
            tabs_[StTabName::kStNetwork]->SetAttach(btn_network_);
            tabs_[StTabName::kStSecurity]->SetAttach(btn_security_);
            tabs_[StTabName::kStController]->SetAttach(btn_controller_);
            tabs_[StTabName::kStAboutMe]->SetAttach(btn_about_me_);

            auto layout = new NoMarginVLayout();
            // title margin
            //layout->addSpacing(kTabContentMarginTop);

            auto stack_widget = new QStackedWidget(this);
            stack_widget->addWidget(tabs_[StTabName::kStGeneral]);
            stack_widget->addWidget(tabs_[StTabName::kStNetwork]);
            stack_widget->addWidget(tabs_[StTabName::kStSecurity]);
            stack_widget->addWidget(tabs_[StTabName::kStController]);
            stack_widget->addWidget(tabs_[StTabName::kStAboutMe]);
            stacked_widget_ = stack_widget;
            layout->addWidget(stack_widget);
            root_layout->addSpacing(40);
            root_layout->addLayout(layout);
        }

        setLayout(root_layout);
        ChangeTab(StTabName::kStGeneral);
    }

    TabSettings::~TabSettings() {
    }

    void TabSettings::OnTabShow() {
    }

    void TabSettings::OnTabHide() {
    }

    void TabSettings::ChangeTab(const StTabName& tn) {
        for (auto& [name, tab] : tabs_) {
            if (tn == name) {
                stacked_widget_->setCurrentWidget(tabs_[tn]);
                tabs_[tn]->OnTabShow();
                const QPointer<CustomTabBtn> button(dynamic_cast<CustomTabBtn*>(tabs_[tn]->GetAttach()));
                if (button) {
                    button->ToActiveStatus();
                }
            } else {
                tabs_[name]->OnTabHide();
                const QPointer<CustomTabBtn> button(dynamic_cast<CustomTabBtn*>(tabs_[name]->GetAttach()));
                if (button) {
                    button->ToInActiveStatus();
                }
            }
        }
    }

}
