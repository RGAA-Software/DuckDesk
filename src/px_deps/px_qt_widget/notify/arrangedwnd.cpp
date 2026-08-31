#include "arrangedwnd.h"
#include "notifymanager.h"

namespace px
{

    static QPointer<QPropertyAnimation> propertyAnimationOnTarget(
        const QPointer<ArrangedWnd>& target, const QByteArray& propertyName,
        const QVariant& endValue, int duration) {
        if (!target) {
            return {};
        }
        const QPointer<QPropertyAnimation> animation(
            new QPropertyAnimation(target, propertyName, target)); // NOLINT(gammaray-raw-pointer-boundary) Qt target owns the animation.
        animation->setStartValue(target->property(propertyName));
        animation->setEndValue(endValue);
        animation->setDuration(duration);
        animation->start(QAbstractAnimation::DeleteWhenStopped);
        return animation;
    }

    template<typename func>
    static inline void
    propertyAnimationOnTarget(const QPointer<ArrangedWnd>& target,
                              const QByteArray& propertyName,
                              const QVariant& endValue, int duration,
                              func onFinished) {
        const auto animation = propertyAnimationOnTarget(
            target, propertyName, endValue, duration);
        if (!animation || !target) {
            return;
        }
        QObject::connect(animation, &QPropertyAnimation::finished, target, onFinished);
    }

    ArrangedWnd::ArrangedWnd(NotifyManager *manager, QWidget *parent)
            : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint) {
        m_manager = manager;
        m_posIndex = 0;

        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setFixedSize(manager->notifyWndSize());

        connect(manager, &QObject::destroyed, this, &QObject::deleteLater);
    }

    void ArrangedWnd::mousePressEvent(QMouseEvent *event) {
        switch (event->button()) {
            case Qt::LeftButton:
                emit clicked();
                break;
            case Qt::RightButton:
                emit rclicked();
                break;
            default:
                break;
        }
    }

    void ArrangedWnd::showArranged(int posIndex) {
        const QPointer<ArrangedWnd> guarded_self(this);
        const auto manager = m_manager;
        if (!manager) {
            return;
        }
        if (m_posIndex == posIndex) return;
        m_posIndex = posIndex;
        if (posIndex <= 0) // 隐藏
        {
            if (!isVisible()) return;
            propertyAnimationOnTarget(guarded_self, "windowOpacity", 0,
                manager->animateTime(), [guarded_self]() {
                if (!guarded_self) {
                    return;
                }
                guarded_self->hide();
                emit guarded_self->visibleChanged(false);
            });
            return;
        }

        // 计算提醒框的位置
        QSize wndsize = manager->notifyWndSize();
        QSize offset = QSize(wndsize.width(), wndsize.height() * posIndex + manager->spacing() * (posIndex - 1));
        QPoint pos = manager->cornerPos() - QPoint(offset.width(), offset.height());

        if (!isVisible()) // 显示
        {
            show();
            move(pos);
            setWindowOpacity(0);
            propertyAnimationOnTarget(guarded_self, "windowOpacity", 1,
                manager->animateTime(), [guarded_self]() {
                if (guarded_self) {
                    emit guarded_self->visibleChanged(true);
                }
            });
        } else // 移动位置
        {
            propertyAnimationOnTarget(
                guarded_self, "pos", pos, manager->animateTime());
        }
    }

}
