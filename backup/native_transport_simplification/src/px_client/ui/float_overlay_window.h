#pragma once

#include "base_widget.h"

#include <QPointer>
#include <QRect>
#include <QSize>

namespace px
{

    // A small, owned top-level window composed by DWM above the native
    // D3D/Vulkan render HWND.  The transparent margin belongs to the window
    // and gives the custom shadow room to render without being clipped.
    class FloatOverlayWindow : public BaseWidget {
    public:
        // Keep only the pixels required by the painted shadow. A wider
        // transparent canvas is visible as an empty strip around popup HWNDs.
        static constexpr int kShadowMargin = 6;
        static constexpr int kCardRadius = 8;
        static constexpr int kPopupGap = 6;

        FloatOverlayWindow(const std::shared_ptr<ClientContext>& ctx,
                           QWidget* owner,
                           const QSize& content_size,
                           int card_radius = kCardRadius,
                           int shadow_margin = kShadowMargin,
                           int shadow_offset_y = 0);

        QWidget* OverlayOwner() const;
        QRect OwnerGlobalRect() const;
        QRect CardRect() const;
        QRect VisualRectGlobal() const;
        QSize ContentSize() const;
        int ContentWidth() const;
        int ContentHeight() const;

        void SetContentSize(const QSize& size);
        void MoveClamped(const QPoint& global_top_left);
        void ShowWithoutActivating();
        void ShowBeside(const QRect& anchor_global, bool prefer_right = true);
        void ShowFlyout(const FloatOverlayWindow* parent_popup,
                        const QWidget* anchor_item,
                        bool prefer_right = true);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void showEvent(QShowEvent* event) override;
        void PaintCard(QPainter& painter,
                       const QColor& background = QColor(255, 255, 255),
                       const QColor& border = QColor(225, 228, 234)) const;

    private:
        QPoint ClampTopLeft(const QPoint& global_top_left) const;
        void ApplyNativeWindowStyle();

        QPointer<QWidget> overlay_owner_;
        QSize content_size_;
        int card_radius_ = kCardRadius;
        int shadow_margin_ = kShadowMargin;
        int shadow_offset_y_ = 0;
        bool has_placement_direction_ = false;
        bool opened_to_right_ = true;
    };

}
