#include <gtest/gtest.h>

#include <chrono>
#include <future>

#include "px_common_new/thread.h"

namespace px
{

    TEST(ThreadExitTest, WorkerMayRequestItsOwnExitWithoutJoiningItself) {
        auto worker = Thread::Make("self-exit-test", 4);
        worker->Poll();

        std::promise<void> returned_from_exit;
        auto returned = returned_from_exit.get_future();
        worker->Post([worker, &returned_from_exit]() {
            worker->Exit();
            returned_from_exit.set_value();
        });

        EXPECT_EQ(returned.wait_for(std::chrono::seconds(2)), std::future_status::ready);

        // Reap the finished std::thread from its owner. This must be safe and
        // idempotent after the worker requested exit from inside its task.
        worker->Exit();
        EXPECT_TRUE(worker->IsExit());
    }

}
