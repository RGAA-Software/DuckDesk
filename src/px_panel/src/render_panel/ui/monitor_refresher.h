//
// Created by RGAA on 20/02/2025.
//

#ifndef PX_MONITOR_REFRESHER_H
#define PX_MONITOR_REFRESHER_H

#include <QWidget>
#include <QList>
#include <memory>

namespace px
{

    class PxContext;
    class MessageListener;

    // Widget
    class MonitorRefreshWidget : public QWidget {
    public:
        explicit MonitorRefreshWidget(const std::shared_ptr<PxContext>& ctx, QWidget* parent);
        void paintEvent(QPaintEvent *event) override;

    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        int color_value_ = 0;
    };

    // Refresher
    class MonitorRefresher : public std::enable_shared_from_this<MonitorRefresher> {
    public:
        explicit MonitorRefresher(const std::shared_ptr<PxContext>& ctx, QWidget* parent);
        void InitMessageListeners();
        void Exit();
    private:
        void Refresh();
    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        QList<QWidget*> widgets_;
        std::atomic_bool exit_ = false;
    };

}

#endif //PX_MONITOR_REFRESHER_H
