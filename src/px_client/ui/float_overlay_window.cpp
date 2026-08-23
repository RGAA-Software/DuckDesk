#include "float_overlay_window.h"

#include <QPainterPath>
#include <QScreen>
#include <QShowEvent>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <algorithm>

namespace px
{

    FloatOverlayWindow::FloatOverlayWindow(const std::shared_ptr<ClientContext>& ctx,
                                           QWidget* owner,
                                           const QSize& content_size,
                                           int card_radius,
                                           int shadow_margin,
                                           int shadow_offset_y)
        : BaseWidget(ctx, owner),
          overlay_owner_(owner),
          content_size_(content_size),
          card_radius_(card_radius),
          shadow_margin_(std::max(1, shadow_margin)),
          shadow_offset_y_(std::clamp(shadow_offset_y, 0, std::max(1, shadow_margin))) {
        setWindowFlags(Qt::Tool |
                       Qt::FramelessWindowHint |
                       Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setFocusPolicy(Qt::NoFocus);
        setStyleSheet("background: transparent;");
        SetContentSize(content_size_);

        // Force creation now so the owned/no-activate native styles are in
        // place before the first frame is shown above a native render HWND.
        winId();
        ApplyNativeWindowStyle();
    }

    QWidget* FloatOverlayWindow::OverlayOwner() const {
        return overlay_owner_.data();
    }

    QRect FloatOverlayWindow::OwnerGlobalRect() const {
        if (overlay_owner_) {
            return QRect(overlay_owner_->mapToGlobal(QPoint(0, 0)), overlay_owner_->size());
        }
        if (screen()) {
            return screen()->availableGeometry();
        }
        return {};
    }

    QRect FloatOverlayWindow::CardRect() const {
        return QRect(shadow_margin_, shadow_margin_, content_size_.width(), content_size_.height());
    }

    QRect FloatOverlayWindow::VisualRectGlobal() const {
        return QRect(pos() + QPoint(shadow_margin_, shadow_margin_), content_size_);
    }

    QSize FloatOverlayWindow::ContentSize() const {
        return content_size_;
    }

    int FloatOverlayWindow::ContentWidth() const {
        return content_size_.width();
    }

    int FloatOverlayWindow::ContentHeight() const {
        return content_size_.height();
    }

    void FloatOverlayWindow::SetContentSize(const QSize& size) {
        content_size_ = size;
        setFixedSize(content_size_ + QSize(shadow_margin_ * 2, shadow_margin_ * 2));
        update();
    }

    QPoint FloatOverlayWindow::ClampTopLeft(const QPoint& global_top_left) const {
        const QRect bounds = OwnerGlobalRect();
        if (!bounds.isValid()) {
            return global_top_left;
        }

        constexpr int safety = 2;
        const int min_x = bounds.left() - shadow_margin_ + safety;
        const int min_y = bounds.top() - shadow_margin_ + safety;
        const int max_x = std::max(min_x, bounds.right() - ContentWidth() - shadow_margin_ - safety + 1);
        const int max_y = std::max(min_y, bounds.bottom() - ContentHeight() - shadow_margin_ - safety + 1);
        return QPoint(std::clamp(global_top_left.x(), min_x, max_x),
                      std::clamp(global_top_left.y(), min_y, max_y));
    }

    void FloatOverlayWindow::MoveClamped(const QPoint& global_top_left) {
        move(ClampTopLeft(global_top_left));
    }

    void FloatOverlayWindow::ShowWithoutActivating() {
        show();
        ApplyNativeWindowStyle();
#ifdef Q_OS_WIN
        const auto hwnd = reinterpret_cast<HWND>(winId());
        SetWindowPos(hwnd, HWND_TOP, x(), y(), width(), height(),
                     SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
#else
        raise();
#endif
    }

    void FloatOverlayWindow::ShowBeside(const QRect& anchor_global, bool prefer_right) {
        const QRect bounds = OwnerGlobalRect();
        const int right_x = anchor_global.right() + kPopupGap - shadow_margin_ + 1;
        const int left_x = anchor_global.left() - kPopupGap - ContentWidth() - shadow_margin_;
        const bool fits_right = right_x + width() <= bounds.right() + shadow_margin_ + 1;
        const bool fits_left = left_x >= bounds.left() - shadow_margin_;

        int target_x = right_x;
        if ((prefer_right && !fits_right && fits_left) || (!prefer_right && fits_left)) {
            target_x = left_x;
        }
        opened_to_right_ = target_x == right_x;
        has_placement_direction_ = true;
        MoveClamped(QPoint(target_x, anchor_global.top() - shadow_margin_));
        ShowWithoutActivating();
    }

    void FloatOverlayWindow::ShowFlyout(const FloatOverlayWindow* parent_popup,
                                        const QWidget* anchor_item,
                                        bool prefer_right) {
        if (!parent_popup || !anchor_item) {
            return;
        }

        const QRect parent_card = parent_popup->VisualRectGlobal();
        const QPoint anchor_top = anchor_item->mapToGlobal(QPoint(0, 0));
        const QRect bounds = OwnerGlobalRect();
        const int right_x = parent_card.right() + kPopupGap - shadow_margin_ + 1;
        const int left_x = parent_card.left() - kPopupGap - ContentWidth() - shadow_margin_;
        const bool fits_right = right_x + width() <= bounds.right() + shadow_margin_ + 1;
        const bool fits_left = left_x >= bounds.left() - shadow_margin_;

        // Once a popup chain flips at an edge, keep later levels flowing in
        // that direction. Otherwise a third-level menu can turn back and
        // overlap its grandparent even though there is ample room on the
        // cascade side.
        const bool effective_prefer_right = parent_popup->has_placement_direction_
                                                ? parent_popup->opened_to_right_
                                                : prefer_right;
        int target_x = right_x;
        if ((effective_prefer_right && !fits_right && fits_left)
            || (!effective_prefer_right && fits_left)) {
            target_x = left_x;
        }
        opened_to_right_ = target_x == right_x;
        has_placement_direction_ = true;
        MoveClamped(QPoint(target_x, anchor_top.y() - shadow_margin_));
        ShowWithoutActivating();
    }

    void FloatOverlayWindow::PaintCard(QPainter& painter,
                                       const QColor& background,
                                       const QColor& border) const {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setPen(Qt::NoPen);

        // Paint non-overlapping rings.  Their alpha follows a quadratic falloff
        // and produces a CSS-like soft shadow without QGraphicsEffect crossing
        // the native D3D/Vulkan composition boundary.
        const QRectF card = QRectF(CardRect());
        const QPointF shadow_offset(0.0, shadow_offset_y_);
        for (int distance = shadow_margin_; distance >= 1; --distance) {
            const qreal outer_distance = distance;
            const qreal inner_distance = distance - 1;
            QPainterPath outer;
            outer.addRoundedRect(card.adjusted(-outer_distance, -outer_distance,
                                                outer_distance, outer_distance).translated(shadow_offset),
                                 card_radius_ + outer_distance,
                                 card_radius_ + outer_distance);
            QPainterPath inner;
            inner.addRoundedRect(card.adjusted(-inner_distance, -inner_distance,
                                                inner_distance, inner_distance).translated(shadow_offset),
                                 card_radius_ + inner_distance,
                                 card_radius_ + inner_distance);
            const qreal proximity = 1.0 - (outer_distance - 1.0) / shadow_margin_;
            QColor shadow(0, 0, 0, static_cast<int>(4 + 40 * proximity * proximity));
            painter.fillPath(outer.subtracted(inner), shadow);
        }

        painter.setBrush(background);
        painter.setPen(QPen(border, 1.0));
        painter.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5),
                                card_radius_, card_radius_);
    }

    void FloatOverlayWindow::paintEvent(QPaintEvent* event) {
        QPainter painter(this);
        PaintCard(painter);
        BaseWidget::paintEvent(event);
    }

    void FloatOverlayWindow::showEvent(QShowEvent* event) {
        ApplyNativeWindowStyle();
        BaseWidget::showEvent(event);
    }

    void FloatOverlayWindow::ApplyNativeWindowStyle() {
        if (overlay_owner_ && overlay_owner_->windowHandle() && windowHandle()) {
            windowHandle()->setTransientParent(overlay_owner_->window()->windowHandle());
        }
#ifdef Q_OS_WIN
        const auto hwnd = reinterpret_cast<HWND>(winId());
        auto ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        ex_style |= WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        ex_style &= ~WS_EX_APPWINDOW;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex_style);

        if (overlay_owner_) {
            const auto owner_hwnd = reinterpret_cast<HWND>(overlay_owner_->window()->winId());
            SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner_hwnd));
        }
#endif
    }

}
