#include <functional>
#include <memory>
#include <vector>

#include <QObject>
#include <QPointer>
#include <gtest/gtest.h>

#include "render_panel/ui/qt_lifetime_guard.h"

namespace px
{

    TEST(PanelQtLifetimeGuard, DeliversWhileObjectIsAlive) {
        const auto deliveries = std::make_shared<int>(0);
        auto owner = std::make_unique<QObject>();
        auto callback = MakeQtLifetimeAction(
            QPointer<QObject>(owner.get()),
            [deliveries](const QPointer<QObject>&) {
                ++*deliveries;
            });

        callback();

        EXPECT_EQ(*deliveries, 1);
    }

    TEST(PanelQtLifetimeGuard, RejectsQueuedCallbackAfterObjectDestruction) {
        const auto deliveries = std::make_shared<int>(0);
        std::vector<std::function<void()>> queued_callbacks;

        for (int iteration = 0; iteration < 10; ++iteration) {
            auto owner = std::make_unique<QObject>();
            queued_callbacks.emplace_back(MakeQtLifetimeAction(
                QPointer<QObject>(owner.get()),
                [deliveries](const QPointer<QObject>&) {
                    ++*deliveries;
                }));
            owner.reset();
            queued_callbacks.back()();
        }

        EXPECT_EQ(*deliveries, 0);
    }

}
