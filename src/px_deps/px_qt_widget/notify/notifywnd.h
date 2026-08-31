#ifndef NOTIFYWND_H
#define NOTIFYWND_H

#include "arrangedwnd.h"
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include "notify_defs.h"

namespace px
{

    class TcImageButton;

    class NotifyWnd : public ArrangedWnd {
    Q_OBJECT
    public:
        explicit NotifyWnd(NotifyManager *manager, QWidget *parent = 0);
        NotifyItem data() const;
        void setData(const NotifyItem &data);

    private:
        NotifyItem m_data;
        QPointer<QFrame> background;
        QPointer<QLabel> iconLabel;
        QPointer<QLabel> titleLabel;
        QPointer<QLabel> bodyLabel;
        QPointer<TcImageButton> closeBtn;
    };

    class NotifyCountWnd : public ArrangedWnd {
    Q_OBJECT
    public:
        explicit NotifyCountWnd(NotifyManager *manager, QWidget *parent = 0);
        void setCount(int count);

    private:
        QPointer<QLabel> iconLabel;
        QPointer<QLabel> countLabel;
        QPointer<QPropertyAnimation> flickerAnim;
    };

}

#endif // NOTIFYWND_H
