#include "tab_cloud_apps.h"

#include <QHBoxLayout>
#include <QPointer>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "px_qt_widget/px_image_button.h"
#include "px_qt_widget/px_label.h"
#include "px_qt_widget/widget_helper.h"
#include "render_panel/devices/app_stream_list.h"
#include "render_panel/px_context.h"

namespace px
{
    TabCloudApps::TabCloudApps(
        const std::shared_ptr<PxApplication>& app,
        QWidget* parent) // NOLINT(gammaray-raw-pointer-boundary) Qt parent ABI; TabBase retains ownership.
        : TabBase(app, parent) {
        QPointer<TabCloudApps> self(this);
        auto root_layout = new QVBoxLayout();
        WidgetHelper::ClearMargins(root_layout);
        root_layout->addSpacing(kTabContentMarginTop + 20);

        auto title_layout = new QHBoxLayout();
        WidgetHelper::ClearMargins(title_layout);
        title_layout->addSpacing(30);

        auto title = new TcLabel(this);
        title->SetTextId("id_tab_cloud_applications");
        title->setAlignment(Qt::AlignLeft);
        title->setStyleSheet("font-size: 22px; font-weight: 700;");
        title_layout->addWidget(title);

        auto refresh = new TcImageButton(":/resources/image/ic_refresh.svg", QSize(20, 20));
        refresh->SetColor(0xffffff, 0xdddddd, 0xbbbbbb);
        refresh->SetRoundRadius(15);
        refresh->setFixedSize(30, 30);
        title_layout->addSpacing(20);
        title_layout->addWidget(refresh);
        title_layout->addStretch();
        root_layout->addLayout(title_layout);

        app_list_ = new AppStreamList(
            context_,
            AppStreamListMode::kCloudApplications,
            [self](bool empty) {
                if (self) {
                    self->SetEmpty(empty);
                }
            },
            this);
        root_layout->addWidget(app_list_);
        setLayout(root_layout);

        empty_tip_ = new QLabel(this);
        constexpr int empty_size = 64;
        empty_tip_->resize(empty_size, empty_size);
        auto pixmap = QPixmap::fromImage(QImage(":/resources/image/empty.svg"));
        empty_tip_->setPixmap(pixmap.scaled(empty_size, empty_size,
                                            Qt::KeepAspectRatio, Qt::SmoothTransformation));
        empty_tip_->show();

        refresh->SetOnImageButtonClicked([self]() {
            if (self) {
                self->RefreshApplications();
            }
        });
        app_list_->LoadStreamItems();
    }

    TabCloudApps::~TabCloudApps() = default;

    void TabCloudApps::OnTabShow() {
        RefreshApplications();
    }

    void TabCloudApps::RefreshApplications() {
        if (app_list_) {
            app_list_->RefreshResources();
        }
    }

    void TabCloudApps::SetEmpty(bool empty) {
        if (empty_tip_) {
            empty_tip_->setVisible(empty);
            if (empty) empty_tip_->raise();
        }
    }

    void TabCloudApps::resizeEvent(QResizeEvent* event) {
        TabBase::resizeEvent(event);
        if (!empty_tip_) return;
        empty_tip_->move((event->size().width() - empty_tip_->width()) / 2,
                         (event->size().height() - empty_tip_->height()) / 2);
    }
}
