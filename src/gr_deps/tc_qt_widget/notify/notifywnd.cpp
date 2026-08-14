#include "notifywnd.h"
#include "notifymanager.h"
#include <QBoxLayout>
#include <QGraphicsDropShadowEffect>
#include "no_margin_layout.h"
#include "tc_qt_widget/tc_image_button.h"

namespace tc
{

    NotifyWnd::NotifyWnd(NotifyManager *manager, QWidget *parent)
            : ArrangedWnd(manager, parent) {
        background = new QFrame(this);
        background->setGeometry(3, 3, width() - 6, height() - 6);
        background->setObjectName("notify-background");

        QHBoxLayout *mainLayout = new NoMarginHLayout();
        QVBoxLayout *contentLayout = new NoMarginVLayout();

        auto icon_layout = new NoMarginVLayout();
        iconLabel = new QLabel(background);
        iconLabel->setFixedSize(40, 40);
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setWordWrap(true);
        icon_layout->addStretch();
        icon_layout->addWidget(iconLabel);
        icon_layout->addStretch();

        titleLabel = new QLabel(background);
        titleLabel->setObjectName("notify-title");

        bodyLabel = new QLabel(background);
        bodyLabel->setObjectName("notify-body");
        bodyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        bodyLabel->setWordWrap(true);

        contentLayout->addSpacing(15);
        contentLayout->addWidget(titleLabel);
        contentLayout->addSpacing(6);
        contentLayout->addWidget(bodyLabel);
        contentLayout->addStretch();

        mainLayout->addSpacing(10);
        mainLayout->addLayout(icon_layout);
        mainLayout->addSpacing(12);
        mainLayout->addLayout(contentLayout);
        mainLayout->setAlignment(iconLabel, Qt::AlignTop);

        setLayout(mainLayout);

        closeBtn = new TcImageButton(":/resources/image/ic_close.svg", QSize(20, 20), background);
        closeBtn->SetColor(0xffffff, 0xdddddd, 0xaaaaaa);
        closeBtn->setObjectName("notify-close-btn");
        closeBtn->setFixedSize(28, 28);
        closeBtn->move(background->width() - closeBtn->width() - 5, 5);
        closeBtn->SetOnImageButtonClicked([=, this]() {
            this->deleteLater();
        });

        setStyleSheet(m_manager->styleSheet());

        auto shadow = new QGraphicsDropShadowEffect(this);
        shadow->setOffset(0, 0);
        shadow->setBlurRadius(5);
        background->setGraphicsEffect(shadow);

        connect(this, &ArrangedWnd::visibleChanged, [this](bool visible) {
            if (visible) {
                int displayTime = m_manager->displayTime();//m_data.value("displayTime", m_manager->displayTime()).toInt();
                QTimer::singleShot(displayTime, this, [this]() {
                    showArranged(0);
                });
            } else deleteLater();
        });
    }

    NotifyItem NotifyWnd::data() const {
        return m_data;
    }

    void NotifyWnd::setData(const NotifyItem& data) {
        m_data = data;

        auto icon = QPixmap(m_manager->defaultIcon());
        if (data.type_ == NotifyItemType::kError) {
            icon = QPixmap(m_manager->errorIcon());
        }
        else if (data.type_ == NotifyItemType::kWarning) {
            icon = QPixmap(m_manager->warningIcon());
        }

        icon = icon.scaled(QSize(32, 32), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        iconLabel->setPixmap(icon);

        QString title = data.title_;
        titleLabel->setText(title);

        QString body = m_data.body_;
        bodyLabel->setText(body);
    }

    NotifyCountWnd::NotifyCountWnd(NotifyManager *manager, QWidget *parent)
            : ArrangedWnd(manager, parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);

        iconLabel = new QLabel(this);
        iconLabel->setFixedSize(40, 40);
        iconLabel->setAlignment(Qt::AlignCenter);

        countLabel = new QLabel(this);
        countLabel->setObjectName("notify-count");
        countLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        auto mainLayout = new QHBoxLayout(this);
        mainLayout->addWidget(iconLabel);
        mainLayout->addWidget(countLabel);

        auto shadow = new QGraphicsDropShadowEffect(this);
        shadow->setOffset(2, 2);
        shadow->setBlurRadius(5);
        setGraphicsEffect(shadow);

        setStyleSheet("#notify-count {"
                      "font: 20px Verdana;"
                      "color: #dd424d;"
                      "}");

        QPixmap icon = QPixmap(m_manager->defaultIcon());
        icon = icon.scaled(QSize(32, 32), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        iconLabel->setPixmap(icon);

        flickerAnim = new QPropertyAnimation(this, "windowOpacity", this);
        flickerAnim->setStartValue(1);
        flickerAnim->setKeyValueAt(0.25, 0.1);
        flickerAnim->setKeyValueAt(0.5, 1);
        flickerAnim->setEndValue(1);
        flickerAnim->setDuration(2000);
        flickerAnim->setLoopCount(-1);

        connect(this, &ArrangedWnd::visibleChanged, [this](bool visible) {
            if (visible) flickerAnim->start();
            else flickerAnim->stop();
        });
    }

    void NotifyCountWnd::setCount(int count) {
        countLabel->setNum(count);
    }

}