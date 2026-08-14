#ifndef NOTIFYWND_H
#define NOTIFYWND_H

#include "arrangedwnd.h"
#include <QLabel>
#include <QPushButton>
#include "notify_defs.h"

namespace tc
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
        QFrame *background;
        QLabel *iconLabel;
        QLabel *titleLabel;
        QLabel *bodyLabel;
        TcImageButton *closeBtn;
    };

    class NotifyCountWnd : public ArrangedWnd {
    Q_OBJECT
    public:
        explicit NotifyCountWnd(NotifyManager *manager, QWidget *parent = 0);
        void setCount(int count);

    private:
        QLabel *iconLabel;
        QLabel *countLabel;
        QPropertyAnimation *flickerAnim;
    };

}

#endif // NOTIFYWND_H
