#ifndef NOTIFYMANAGER_H
#define NOTIFYMANAGER_H

#include <QtCore>
#include "notify_defs.h"

namespace tc
{

    class NotifyWnd;
    class NotifyCountWnd;

    class NotifyManager : public QObject {
    Q_OBJECT

    public:
        explicit NotifyManager(QWidget *parent = nullptr);

        void notify(const NotifyItem& item);
        void notifyErr(const NotifyItem& item);

        void setMaxCount(int count);
        int displayTime() const;
        void setDisplayTime(int displayTime);
        int animateTime() const;
        void setAnimateTime(int animateTime);
        int spacing() const;
        void setSpacing(int spacing);
        QPoint cornerPos() const;
        void setCornerMargins(int right, int bottom);
        QSize notifyWndSize() const;
        void setNotifyWndSize(int width, int height);
        QString defaultIcon() const;
        void setDefaultIcon(const QString &defaultIcon);
        QString errorIcon() const;
        QString warningIcon() const;
        QString styleSheet(const QString &theme = "default") const;
        void setStyleSheet(const QString &styleSheet, const QString &theme = "default");
        void setShowQueueCount(bool isShowQueueCount);

    signals:
        void notifyDetail(const NotifyItem& data);

    private:
        void showNext();

        void showQueueCount();

        QQueue<NotifyItem> m_dataQueue;
        QList<NotifyWnd *> m_notifyList;
        NotifyCountWnd *m_notifyCount;

        int m_maxCount;
        bool m_isShowQueueCount;
        int m_displayTime;
        int m_animateTime;
        int m_spacing;
        QPoint m_cornerPos;
        QSize m_notifyWndSize;
        QString m_defaultIcon;
        QString m_errorIcon;
        QString m_warningIcon;
        QMap<QString, QString> m_styleSheets;
    };

}

#endif // NOTIFYMANAGER_H
