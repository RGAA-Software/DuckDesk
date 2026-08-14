#include "notifymanager.h"
#include "notifywnd.h"
#include <QApplication>
#include <QScreen>

namespace tc
{

    NotifyManager::NotifyManager(QWidget* parent) : QObject(parent) {
        m_maxCount = 5;
        m_displayTime = 10000;
        m_animateTime = 300;
        m_spacing = 10;
        m_notifyWndSize = QSize(350, 80);
        m_defaultIcon = ":/resources/image/ic_notify.png";
        m_errorIcon = ":/resources/image/ic_notify_err.png";
        m_warningIcon = ":/resources/image/ic_notify_warn.png";
        QString def_style =
                "#notify-background {"
                "background: white;"
                "border-radius: 4px;"
                "}"
                "#notify-title{"
                "font-weight: bold;"
                "font-size: %1px;"
                "color: #333333;"
                "}"
                "#notify-body{"
                "font-size: %2px;"
                "color: #444444;"
                "}"
                "#notify-close-btn{ "
                "border: 0;"
                "color: #999999;"
                "}"
                "#notify-close-btn:hover{ "
                "background: #cccccc;"
                "}";
        def_style = def_style.arg(14).arg(12);
        m_styleSheets["default"] = def_style;
        m_isShowQueueCount = true;

        setCornerMargins(10, 10);
        m_notifyCount = new NotifyCountWnd(this);
    }

    void NotifyManager::notify(const NotifyItem& item) {
        m_dataQueue.enqueue(item);
        showNext();
    }

    void NotifyManager::notifyErr(const NotifyItem& item) {
        m_dataQueue.enqueue(item);
        showNext();
    }

    void NotifyManager::setMaxCount(int count) {
        m_maxCount = count;
    }

    int NotifyManager::displayTime() const {
        return m_displayTime;
    }

    void NotifyManager::setDisplayTime(int displayTime) {
        m_displayTime = displayTime;
    }

    int NotifyManager::animateTime() const {
        return m_animateTime;
    }

    void NotifyManager::setAnimateTime(int animateTime) {
        m_animateTime = animateTime;
    }

    int NotifyManager::spacing() const {
        return m_spacing;
    }

    void NotifyManager::setSpacing(int spacing) {
        m_spacing = spacing;
    }

    QPoint NotifyManager::cornerPos() const {
        return m_cornerPos;
    }

    void NotifyManager::setCornerMargins(int right, int bottom) {
        QRect desktopRect = QApplication::primaryScreen()->availableGeometry();
        QPoint bottomRignt = desktopRect.bottomRight();
        m_cornerPos = QPoint(bottomRignt.x() - right, bottomRignt.y() - bottom);
    }

    QSize NotifyManager::notifyWndSize() const {
        return m_notifyWndSize;
    }

    void NotifyManager::setNotifyWndSize(int width, int height) {
        m_notifyWndSize = QSize(width, height);
    }

    QString NotifyManager::defaultIcon() const {
        return m_defaultIcon;
    }

    void NotifyManager::setDefaultIcon(const QString &defaultIcon) {
        m_defaultIcon = defaultIcon;
    }

    QString NotifyManager::errorIcon() const {
        return m_errorIcon;
    }

    QString NotifyManager::warningIcon() const {
        return m_warningIcon;
    }

    QString NotifyManager::styleSheet(const QString &theme) const {
        if (!m_styleSheets.contains(theme)) return m_styleSheets.value("default");
        return m_styleSheets.value(theme);
    }

    void NotifyManager::setStyleSheet(const QString &styleSheet, const QString &theme) {
        m_styleSheets[theme] = styleSheet;
    }

    void NotifyManager::setShowQueueCount(bool isShowQueueCount) {
        m_isShowQueueCount = isShowQueueCount;
        if (!m_isShowQueueCount) m_notifyCount->showArranged(0);
    }

    void NotifyManager::showNext() {
        if (m_notifyList.size() >= m_maxCount || m_dataQueue.isEmpty()) {
            showQueueCount();
            return;
        }

        auto notify = new NotifyWnd(this, (QWidget*)parent());
        m_notifyList.append(notify);
        notify->showArranged(m_notifyList.size());
        notify->setData(m_dataQueue.dequeue());
        showQueueCount();

        connect(notify, &QObject::destroyed, this, [notify, this]() {
            int index = m_notifyList.indexOf(notify);
            m_notifyList.removeAt(index);
            for (; index < m_notifyList.size(); index++)
                m_notifyList[index]->showArranged(index + 1);
            QTimer::singleShot(m_animateTime, this, [this]() {
                showNext();
            });
        });

        connect(notify, &ArrangedWnd::clicked, this, [notify, this]() {
            notify->deleteLater();
            auto data = notify->data();
            QTimer::singleShot(0, this, [this, data]() {
                emit notifyDetail(data); // 保证窗口先销毁，避免模式窗口阻塞事件
                if (data.cbk_) {
                    data.cbk_();
                }
            });
        });

        connect(notify, &ArrangedWnd::rclicked, this, [notify]() {
            notify->deleteLater();
        });
    }

    void NotifyManager::showQueueCount() {
        if (!m_isShowQueueCount) return;
        if (!m_dataQueue.isEmpty()) {
            m_notifyCount->showArranged(m_maxCount + 1);
            m_notifyCount->setCount(m_dataQueue.size());
        } else m_notifyCount->showArranged(0);
    }

}