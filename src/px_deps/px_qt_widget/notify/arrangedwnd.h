#ifndef ARRANGEWND_H
#define ARRANGEWND_H

#include <QWidget>
#include <QMouseEvent>
#include <QPointer>
#include <QPropertyAnimation>

namespace px
{

    class NotifyManager;

    class ArrangedWnd : public QWidget {
    Q_OBJECT
    public:
        explicit ArrangedWnd(NotifyManager *manager, QWidget *parent = 0);
        void mousePressEvent(QMouseEvent *event);
        void showArranged(int posIndex);

    signals:
        void clicked();
        void rclicked();
        void visibleChanged(bool visible);

    protected:
        QPointer<NotifyManager> m_manager;
        int m_posIndex;
    };

}

#endif // ARRANGEWND_H
