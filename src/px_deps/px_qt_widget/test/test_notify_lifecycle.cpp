#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QPointer>
#include <QThread>
#include <QWidget>

#include <memory>

#include "notify/notifymanager.h"

namespace px {
namespace {

TEST(NotifyLifecycle, ParentDestructionCancelsQueuedAnimationAndTimerCallbacks) {
    constexpr int kCycles = 20;  // Exercise repeated create/queue/destroy cycles.
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        auto parent = std::make_unique<QWidget>();
        const QPointer<NotifyManager> manager(
            new NotifyManager(parent.get())); // NOLINT(gammaray-raw-pointer-boundary) QWidget parent owns the manager.
        manager->setAnimateTime(0);
        manager->setDisplayTime(1);

        NotifyItem item;
        item.type_ = NotifyItemType::kNormal;
        item.title_ = "title";
        item.body_ = "body";
        manager->notify(item);
        QCoreApplication::processEvents();

        parent.reset();
        EXPECT_FALSE(manager);
        QThread::msleep(2);
        QCoreApplication::processEvents();
    }
}

}  // namespace
}  // namespace px

int main(int argc, char** argv) { // NOLINT(gammaray-raw-pointer-boundary) Process entry-point ABI.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
