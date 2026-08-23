//
// Created by RGAA on 2023/8/19.
//

#include "stream_item_widget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QToolTip>
#include <QMouseEvent>
#include <QLabel>
#include <QGraphicsDropShadowEffect>
#include <QVBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "px_console_client/console_stream.h"
#include "px_common_new/uid_spacer.h"
#include "px_qt_widget/px_image_button.h"
#include "px_qt_widget/px_font_manager.h"
#include "px_qt_widget/px_pushbutton.h"
#include "px_qt_widget/px_label.h"
#include "px_qt_widget/translator/px_translator.h"
#include "px_base/ct_stream_item_net_type.h"

namespace px
{

    namespace {

        bool IsConsoleApplication(const std::shared_ptr<px_console::ConsoleStream>& item) {
            return item && item->connect_type_ == "console_app_ticket";
        }

        const char* ConsoleApplicationStateTextId(const std::string& state) {
            if (state == "running") return "id_state_running";
            if (state == "starting") return "id_state_starting";
            if (state == "stopping") return "id_state_stopping";
            if (state == "failed") return "id_state_failed";
            return "id_state_stopped";
        }

    }

    // 状态点 hover 提示框:白底黑字、柔影、扁平风(手绘背景)
    class StateToolTip : public QLabel {
    public:
        explicit StateToolTip(QWidget* parent = nullptr) : QLabel(parent) {}
    protected:
        void paintEvent(QPaintEvent* ev) override {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xffffff));
            p.drawRoundedRect(rect(), 4, 4);
            QLabel::paintEvent(ev);
        }
    };

    StreamItemWidget::StreamItemWidget(const std::shared_ptr<px_console::ConsoleStream>& item, int bg_color, QWidget* parent) : QWidget(parent) {
        this->item_ = item;
        this->bg_color_ = bg_color;
        this->setStyleSheet("background:#00000000;");
        // 右上三个状态点 hover 提示需要持续追踪鼠标
        this->setMouseTracking(true);

        // 自定义 tooltip:白底黑字 + 边框 + 阴影
        // 顶层窗口尺寸即 label 大小,阴影画在窗口外会被裁掉,
        // 所以外套一层带透明边距的容器,把阴影画在容器内。
        state_tooltip_container_ = new QWidget(nullptr);
        state_tooltip_container_->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
        state_tooltip_container_->setAttribute(Qt::WA_TranslucentBackground);
        state_tooltip_container_->setAttribute(Qt::WA_ShowWithoutActivating);
        auto tooltip_layout = new QVBoxLayout(state_tooltip_container_);
        tooltip_layout->setContentsMargins(10, 10, 10, 10);
        tooltip_layout->setSpacing(0);

        state_tooltip_ = new StateToolTip(state_tooltip_container_);
        state_tooltip_->setStyleSheet(R"(
            QLabel {
                color: #111111;
                padding: 8px 12px;
                font-size: 12px;
            }
        )");
        auto shadow = new QGraphicsDropShadowEffect(state_tooltip_container_);
        shadow->setBlurRadius(24);
        shadow->setOffset(0, 4);
        shadow->setColor(QColor(0, 0, 0, 80));
        state_tooltip_->setGraphicsEffect(shadow);
        tooltip_layout->addWidget(state_tooltip_);
        state_tooltip_container_->hide();
        if (icon_.isNull()) {
            if (IsConsoleApplication(item)) {
                icon_ = QPixmap::fromImage(QImage(":/resources/image/ic_game_normal.svg"));
            } else if (item->HasRelayInfo()) {
                icon_ = QPixmap::fromImage(QImage(":/resources/image/ic_windows_relay.svg"));
            } else {
                icon_ = QPixmap::fromImage(QImage(":/resources/image/ic_windows_direct.svg"));
            }
            icon_ = icon_.scaled(icon_.width() / 6.2, icon_.height() / 6.2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        if (bg_pixmap_.isNull()) {
            bg_pixmap_ = QPixmap::fromImage(QImage(":/resources/image/test_cover.png"));
            bg_pixmap_ = bg_pixmap_.scaled(230, bg_pixmap_.height()*0.65, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (IsConsoleApplication(item) && !item->console_cover_url_.empty()) {
            auto manager = new QNetworkAccessManager(this);
            auto reply = manager->get(QNetworkRequest(QUrl(QString::fromStdString(item->console_cover_url_))));
            connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                QPixmap downloaded;
                if (reply->error() == QNetworkReply::NoError
                    && downloaded.loadFromData(reply->readAll())) {
                    bg_pixmap_ = downloaded.scaled(230, 150, Qt::KeepAspectRatioByExpanding,
                                                   Qt::SmoothTransformation);
                    update();
                }
                reply->deleteLater();
            });
        }

        // connect button
        auto btn_conn = new TcPushButton(this);
        btn_conn_ = btn_conn;
        // 在ListView中，单独设置一下
        btn_conn->setStyleSheet(R"(
            QPushButton {
                background-color:#2979ff;
                color: white;
            }
            QPushButton:hover{
                background-color:#2059ee;
            }
            QPushButton:pressed{
                background-color:#1549dd;
            }
        )");
        btn_conn_->setFixedSize(IsConsoleApplication(item) ? 115 : 70, 25);
        btn_conn_->SetTextId(IsConsoleApplication(item)
                                 ? (item->console_instance_state_ == "running"
                                        ? "id_enter_application"
                                        : "id_start_application")
                                 : "id_connect");

        lbl_connecting_ = new TcLabel(this);
        lbl_connecting_->setFixedSize(100, 25);
        lbl_connecting_->SetTextId("id_connecting");
        lbl_connecting_->hide();

        connect(btn_conn, &QPushButton::clicked, this, [=, this]() {
            if (conn_listener_) {
                conn_listener_();
            }
        });

        auto btn_option = new TcImageButton(":/resources/image/ic_vert_dots.svg", QSize(22, 22), this);
        btn_option->SetColor(0, 0xf6f6f6, 0xeeeeee);
        btn_option->SetRoundRadius(15);
        btn_option->setFixedSize(25, 25);
        btn_option_ = btn_option;
        btn_option->SetOnImageButtonClicked([=, this]() {
            if (menu_listener_) {
                menu_listener_();
            }
        });

        // work mode
        work_mode_ = new TcLabel(this);
//        if (item->network_type_ == kStreamItemNtTypeWebSocket) {
//            // direct
//            work_mode_->SetTextId("id_direct");
//            work_mode_->setStyleSheet("font-size: 13px; font-weight: 700; color: #3e6682;");
//        }
//        else {
//            // relay
//            work_mode_->SetTextId("id_relay");
//            work_mode_->setStyleSheet("font-size: 13px; font-weight: 700; color: #438761;");
//        }
    }

    StreamItemWidget::~StreamItemWidget() {

    }

    void StreamItemWidget::ShowConnecting() {
        lbl_connecting_->show();
        QTimer::singleShot(2000, this, [this]() {
            lbl_connecting_->hide();
        });
    }

    void StreamItemWidget::paintEvent(QPaintEvent *event) {
        const bool is_console_app = IsConsoleApplication(item_);
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing, true);
        painter.setRenderHints(QPainter::TextAntialiasing, true);
        painter.setRenderHints(QPainter::SmoothPixmapTransform, true);
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QBrush(0xffffff));
            painter.drawRoundedRect(0, 0, width(), height(), radius_, radius_);
        }

        {
            painter.save();
            int width = this->width();
            QRect info_rect(0, 0, width, bg_pixmap_.height() + 7);

            QPainterPath path;
            path.setFillRule(Qt::WindingFill);
            path.addRoundedRect(info_rect, radius_, radius_);
            QRect temp_rect(info_rect.left(), info_rect.top()+info_rect.height()/2, info_rect.width(), info_rect.height()/2);
            path.addRect(temp_rect);
            painter.fillPath(path,  QBrush(QColor(0xff, 0xff, 0xff)));
            path.closeSubpath();

            painter.setClipPath(path);
            painter.drawPixmap(info_rect, bg_pixmap_);

            painter.setBrush(QBrush(QColor(0xff, 0xff, 0xff, 0xdd)));
            painter.drawPath(path);
            painter.restore();
        }

        int border_width = 2;
        // stream name
        {
            QFont font(tcFontMgr()->font_name_);
            font.setBold(true);
            font.setStyleStrategy(QFont::PreferAntialias);
            font.setPointSize(13);
            painter.setFont(font);
            //painter.setPen(QPen(QColor(0x555555)));
            painter.setPen(QPen(QColor(0x2979ff)));
            auto stream_name = item_->stream_name_;
            if (is_console_app) {
                stream_name = item_->stream_name_;
            } else if (item_->HasRelayInfo()) {
                stream_name = px::SpaceId(item_->remote_device_id_);
            }
            else {
                stream_name = item_->stream_host_;
            }
            painter.drawText(QRect(15, 0, this->width(), 40), Qt::AlignVCenter, stream_name.c_str());
        }

        int y_offset = 32;
        if (is_console_app) {
            QFont font(tcFontMgr()->font_name_);
            font.setBold(true);
            font.setStyleStrategy(QFont::PreferAntialias);
            font.setPointSize(10);
            painter.setFont(font);
            painter.setPen(QPen(QColor(0x777777)));
            painter.drawText(QRect(15, y_offset, this->width(), 20), Qt::AlignVCenter,
                             tcTr(item_->console_access_mode_ == "public"
                                      ? "id_public_application"
                                      : "id_authorized_application"));
            y_offset += 20;
            painter.drawText(QRect(15, y_offset, this->width(), 20), Qt::AlignVCenter,
                             tcTr(ConsoleApplicationStateTextId(item_->console_instance_state_)));
            y_offset += 20;
        } else if (!item_->stream_name_.empty() && item_->stream_name_ != item_->stream_host_) {
            QFont font(tcFontMgr()->font_name_);
            font.setBold(true);
            font.setStyleStrategy(QFont::PreferAntialias);
            font.setPointSize(10);
            painter.setFont(font);
            painter.setPen(QPen(QColor(0x77777777)));
            painter.drawText(QRect(15, y_offset, this->width(), 20), Qt::AlignVCenter, item_->stream_name_.c_str());
            y_offset += 20;
        }

        // desktop name
        if (!is_console_app) {
            QFont font(tcFontMgr()->font_name_);
            font.setBold(false);
            font.setStyleStrategy(QFont::PreferAntialias);
            font.setPointSize(10);
            painter.setFont(font);
            painter.setPen(QPen(QColor(0x77777777)));
            auto desktop_name = item_->desktop_name_;
            if (item_->HasRelayInfo()) {
                desktop_name = px::SpaceId(desktop_name);
            }
            painter.drawText(QRect(15, y_offset, this->width(), 20), Qt::AlignVCenter, desktop_name.c_str());
            y_offset += 20;
        }

        // os version
        if (!is_console_app) {
            QFont font(tcFontMgr()->font_name_);
            font.setBold(false);
            font.setStyleStrategy(QFont::PreferAntialias);
            font.setPointSize(10);
            painter.setFont(font);
            painter.setPen(QPen(QColor(0x77777777)));
            auto os_version = QString::fromStdString(item_->os_version_);
            os_version = os_version.toUpper();
            painter.drawText(QRect(15, y_offset, this->width(), 20), Qt::AlignVCenter, os_version);
        }

        QPen pen;
        if (enter_) {
            pen.setColor(QColor(0x2979ff));
        } else {
            pen.setColor(QColor(0xffffff));
        }
        pen.setWidth(border_width);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(border_width/2, border_width/2, width() - border_width, height() - border_width, radius_-2, radius_-2);

        int margin_top = 30;
        painter.drawPixmap(QWidget::width() - icon_.width() - 20, margin_top, icon_);

        if (is_console_app) {
            const bool running = item_->console_instance_state_ == "running";
            const bool transitioning = item_->console_instance_state_ == "starting"
                || item_->console_instance_state_ == "stopping";
            const QColor state_color = running ? QColor(0x20, 0xb2, 0x6b)
                                               : (transitioning ? QColor(0xf2, 0xa9, 0x00)
                                                                : QColor(0x99, 0x99, 0x99));
            painter.setPen(Qt::NoPen);
            painter.setBrush(state_color);
            painter.drawEllipse(this->width() - 27, 12, 8, 8);
            return;
        }

        int indicator_width = 10;
        int indicator_height = 8;
        int indicator_radius = 4;
        int margin_right = 50;
        for (int i = 0; i < 3; i++) {
            if (direct_connected_ && i == 0) {
                painter.setBrush(QBrush(0x00ff00));
            }
            else if (relay_connected_ && i == 1) {
                painter.setBrush(QBrush(0x00ff00));
            }
            else if (console_connected_ && i == 2) {
                painter.setBrush(QBrush(0x00ff00));
            }
            else {
                painter.setBrush(QBrush(0xbbbbbb));
            }
            painter.setPen(Qt::NoPen);
            auto x = this->width() - margin_right + indicator_width * i;
            auto y = 10;
            painter.drawRoundedRect(x, y, indicator_width, indicator_height, 0, 0);
            if (i == 1 || i == 2) {
                QPen pen(QColor(0x555555));
                pen.setWidth(1);
                painter.setPen(pen);
                painter.drawLine(x, y+2 , x, y + indicator_height - 2);
            }
        }

        {
            QPen pen(QColor(0x555555));
            pen.setWidth(1);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(this->width() - margin_right, 10, indicator_width * 3, indicator_height, indicator_radius, indicator_radius);
        }
    }

    void StreamItemWidget::enterEvent(QEnterEvent *event) {
        enter_ = true;
        update();
    }

    void StreamItemWidget::leaveEvent(QEvent *event) {
        enter_ = false;
        if (state_tooltip_container_) {
            state_tooltip_container_->hide();
        }
        update();
    }

    void StreamItemWidget::resizeEvent(QResizeEvent *event) {
        QWidget::resizeEvent(event);
        auto y = this->height() - 35;
        btn_conn_->setGeometry(15, y, btn_conn_->width(), btn_conn_->height());
        lbl_connecting_->setGeometry(15* 2 + btn_conn_->width(), y, lbl_connecting_->width(), lbl_connecting_->height());
        btn_option_->setGeometry(this->width() - btn_option_->width() - 13, y, btn_option_->width(), btn_option_->height());
        work_mode_->setGeometry(15 + btn_conn_->width() + 15, y, btn_conn_->width(), btn_conn_->height());
    }

    // 右上三个状态点的 hover 提示(几何与 paintEvent 保持一致)
    void StreamItemWidget::mouseMoveEvent(QMouseEvent *event) {
        QWidget::mouseMoveEvent(event);
        if (IsConsoleApplication(item_)) {
            state_tooltip_container_->hide();
            return;
        }
        static const char* name_ids[3] = {"id_state_direct", "id_state_relay", "id_state_console"};
        const bool states[3] = {direct_connected_, relay_connected_, console_connected_};
        const int margin_right = 50;
        const int indicator_width = 10;
        const int indicator_height = 8;
        const int y = 10;
        // 整个三点区域统一 hover:三行显示全部状态
        QRect area(this->width() - margin_right, y, indicator_width * 3, indicator_height);
        area.adjust(-3, -4, 3, 4);
        const auto pos = event->position().toPoint();
        if (area.contains(pos)) {
            QStringList lines;
            for (int i = 0; i < 3; i++) {
                lines << QString("%1: %2")
                        .arg(tcTr(name_ids[i]))
                        .arg(tcTr(states[i] ? "id_state_available" : "id_state_unavailable"));
            }
            state_tooltip_->setText(lines.join("\n"));
            state_tooltip_->adjustSize();
            state_tooltip_container_->adjustSize();
            state_tooltip_container_->move(event->globalPosition().toPoint() + QPoint(4, 4));
            if (!state_tooltip_container_->isVisible()) {
                state_tooltip_container_->show();
            }
            state_tooltip_container_->raise();
            return;
        }
        state_tooltip_container_->hide();
    }

    void StreamItemWidget::SetOnConnectListener(OnConnectListener&& listener) {
        conn_listener_ = listener;
    }

    void StreamItemWidget::SetOnMenuListener(OnMenuListener&& listener) {
        menu_listener_ = listener;
    }

    void StreamItemWidget::SetDirectConnectedState(bool connected) {
        direct_connected_ = connected;
    }

    void StreamItemWidget::SetRelayConnectedState(bool connected) {
        relay_connected_ = connected;
    }

    void StreamItemWidget::SetConsoleConnectedState(bool connected) {
        console_connected_ = connected;
    }

    void StreamItemWidget::Update() {
        if (IsConsoleApplication(item_)) {
            btn_conn_->SetTextId(item_->console_instance_state_ == "running"
                                     ? "id_enter_application"
                                     : "id_start_application");
        }
        this->update();
    }

    std::string StreamItemWidget::GetStreamId() {
        return item_->stream_id_;
    }

}
