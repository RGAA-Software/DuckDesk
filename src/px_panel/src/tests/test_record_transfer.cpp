//
// Created by RGAA on 2026/08/17.
//
// Unit tests for the record-fetch tunnel queue / retry / url parser
// (docs/console_render_records_view_design.md section 6.2 / 7.2).
//

#include <gtest/gtest.h>

#include <atomic>
#include <format>
#include <memory>
#include <thread>
#include <vector>
#include <fstream>

#include "render_panel/network/record_transfer.h"

using namespace px;

static RecordFetchTask MakeTask(const std::string& filename) {
    RecordFetchTask t;
    t.device_id = "dev_1";
    t.req_id = "req_" + filename;
    t.filename = filename;
    t.token = "tk";
    t.upload_url = "http://10.0.0.1:30300/api/v1/record/upload";
    return t;
}

// ---- retry policy ----

TEST(RecordTransfer, RetryDelayIsExponential) {
    EXPECT_EQ(RecordFetchQueue::RetryDelayMs(1), 2000);
    EXPECT_EQ(RecordFetchQueue::RetryDelayMs(2), 4000);
    EXPECT_EQ(RecordFetchQueue::RetryDelayMs(3), 8000);
    // misuse is clamped, not negative / overflowing
    EXPECT_EQ(RecordFetchQueue::RetryDelayMs(0), 2000);
    EXPECT_EQ(RecordFetchQueue::RetryDelayMs(-5), 2000);
    EXPECT_EQ(RecordFetchQueue::RetryDelayMs(100), RecordFetchQueue::RetryDelayMs(11));
}

// ---- queue semantics ----

TEST(RecordTransfer, FifoOrder) {
    RecordFetchQueue q;
    ASSERT_TRUE(q.Push(MakeTask("a.mp4")));
    ASSERT_TRUE(q.Push(MakeTask("b.mp4")));
    ASSERT_TRUE(q.Push(MakeTask("c.mp4")));
    ASSERT_TRUE(q.TryStartPump());

    RecordFetchTask out;
    ASSERT_TRUE(q.TryPop(out));
    EXPECT_EQ(out.filename, "a.mp4");
    ASSERT_TRUE(q.TryPop(out));
    EXPECT_EQ(out.filename, "b.mp4");
    ASSERT_TRUE(q.TryPop(out));
    EXPECT_EQ(out.filename, "c.mp4");
    EXPECT_FALSE(q.KeepPumpRunning());
}

TEST(RecordTransfer, DedupeSameFilenameWhileInFlight) {
    RecordFetchQueue q;
    ASSERT_TRUE(q.Push(MakeTask("a.mp4")));
    // same file queued again -> rejected
    EXPECT_FALSE(q.Push(MakeTask("a.mp4")));
    EXPECT_TRUE(q.Push(MakeTask("b.mp4")));

    // pop it: still in-flight, re-push must still be rejected
    ASSERT_TRUE(q.TryStartPump());
    RecordFetchTask out;
    ASSERT_TRUE(q.TryPop(out));
    EXPECT_FALSE(q.Push(MakeTask("a.mp4")));

    // after Finish the name is free again
    q.Finish("a.mp4");
    EXPECT_TRUE(q.Push(MakeTask("a.mp4")));
}

TEST(RecordTransfer, RequeueKeepsDedupeAndAppendsAtTail) {
    RecordFetchQueue q;
    ASSERT_TRUE(q.Push(MakeTask("a.mp4")));
    ASSERT_TRUE(q.Push(MakeTask("b.mp4")));
    ASSERT_TRUE(q.TryStartPump());

    RecordFetchTask out;
    ASSERT_TRUE(q.TryPop(out));
    EXPECT_EQ(out.filename, "a.mp4");
    out.attempt = 1;
    ASSERT_TRUE(q.Requeue(out));         // a goes to the tail, still deduped
    EXPECT_FALSE(q.Push(MakeTask("a.mp4")));

    ASSERT_TRUE(q.TryPop(out));
    EXPECT_EQ(out.filename, "b.mp4");    // b comes before re-queued a
    ASSERT_TRUE(q.TryPop(out));
    EXPECT_EQ(out.filename, "a.mp4");
    EXPECT_EQ(out.attempt, 1);
}

TEST(RecordTransfer, ConcurrentPushesStartExactlyOnePump) {
    const auto queue = std::make_shared<RecordFetchQueue>();
    const auto starters = std::make_shared<std::atomic_int>(0);
    std::vector<std::thread> producers;
    for (int index = 0; index < 32; ++index) {
        producers.emplace_back([queue, starters, index]() {
            if (queue->Push(MakeTask(std::format("{}.mp4", index)))
                && queue->TryStartPump()) {
                ++(*starters);
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    EXPECT_EQ(starters->load(), 1);
    EXPECT_EQ(queue->Size(), 32u);
}

TEST(RecordTransfer, ProducerBeforeIdleTransitionKeepsCurrentPump) {
    RecordFetchQueue queue;
    ASSERT_TRUE(queue.Push(MakeTask("a.mp4")));
    ASSERT_TRUE(queue.TryStartPump());
    RecordFetchTask task;
    ASSERT_TRUE(queue.TryPop(task));
    queue.Finish(task.filename);

    ASSERT_TRUE(queue.Push(MakeTask("b.mp4")));
    EXPECT_FALSE(queue.TryStartPump());
    EXPECT_TRUE(queue.KeepPumpRunning());
    ASSERT_TRUE(queue.TryPop(task));
    EXPECT_EQ(task.filename, "b.mp4");
}

TEST(RecordTransfer, ProducerAfterIdleTransitionStartsReplacementPump) {
    RecordFetchQueue queue;
    ASSERT_TRUE(queue.Push(MakeTask("a.mp4")));
    ASSERT_TRUE(queue.TryStartPump());
    RecordFetchTask task;
    ASSERT_TRUE(queue.TryPop(task));
    queue.Finish(task.filename);
    EXPECT_FALSE(queue.KeepPumpRunning());

    ASSERT_TRUE(queue.Push(MakeTask("b.mp4")));
    EXPECT_TRUE(queue.TryStartPump());
}

TEST(RecordTransfer, RepeatedStartStopCyclesAreDeterministic) {
    for (int round = 0; round < 10; ++round) {
        RecordFetchQueue queue;
        ASSERT_TRUE(queue.Push(MakeTask(std::format("{}.mp4", round))));
        ASSERT_TRUE(queue.TryStartPump());
        queue.Stop();
        EXPECT_FALSE(queue.TryStartPump());
        EXPECT_FALSE(queue.KeepPumpRunning());
        EXPECT_EQ(queue.Size(), 0u);
    }
}

TEST(RecordTransfer, AbortAllowsQueuedWorkToStartReplacementPump) {
    RecordFetchQueue queue;
    ASSERT_TRUE(queue.Push(MakeTask("a.mp4")));
    ASSERT_TRUE(queue.TryStartPump());
    queue.AbortPump();
    EXPECT_TRUE(queue.TryStartPump());
}

TEST(RecordTransfer, StopRejectsNewPushAndCancelsQueuedWork) {
    RecordFetchQueue queue;
    ASSERT_TRUE(queue.Push(MakeTask("pending.mp4")));
    ASSERT_TRUE(queue.TryStartPump());
    queue.Stop();
    EXPECT_FALSE(queue.Push(MakeTask("a.mp4")));
    RecordFetchTask out;
    EXPECT_FALSE(queue.TryPop(out));
    EXPECT_FALSE(queue.Requeue(MakeTask("retry.mp4")));
    EXPECT_FALSE(queue.KeepPumpRunning());
    EXPECT_EQ(queue.Size(), 0u);
}

TEST(RecordTransfer, FinishAllowsSameNameAfterPumpBecomesIdle) {
    RecordFetchQueue queue;
    ASSERT_TRUE(queue.Push(MakeTask("same.mp4")));
    ASSERT_TRUE(queue.TryStartPump());
    RecordFetchTask task;
    ASSERT_TRUE(queue.TryPop(task));
    queue.Finish(task.filename);
    EXPECT_FALSE(queue.KeepPumpRunning());
    EXPECT_TRUE(queue.Push(MakeTask("same.mp4")));
    EXPECT_TRUE(queue.TryStartPump());
}

TEST(RecordTransfer, EmptyQueueCannotStartPump) {
    RecordFetchQueue queue;
    EXPECT_FALSE(queue.TryStartPump());
    queue.AbortPump();
    EXPECT_FALSE(queue.TryStartPump());
}

TEST(RecordTransfer, FinishUnknownNameIsHarmless) {
    RecordFetchQueue queue;
    queue.Finish("missing.mp4");
    EXPECT_TRUE(queue.Push(MakeTask("missing.mp4")));
}

TEST(RecordTransfer, StopAfterPopRejectsRetry) {
    RecordFetchQueue queue;
    ASSERT_TRUE(queue.Push(MakeTask("a.mp4")));
    ASSERT_TRUE(queue.TryStartPump());
    RecordFetchTask task;
    ASSERT_TRUE(queue.TryPop(task));
    queue.Stop();
    EXPECT_FALSE(queue.Requeue(task));
    queue.Finish(task.filename);
}

TEST(RecordTransfer, PumpDrainsNewWorkWithoutSecondStarter) {
    RecordFetchQueue queue;
    ASSERT_TRUE(queue.Push(MakeTask("a.mp4")));
    ASSERT_TRUE(queue.TryStartPump());
    RecordFetchTask task;
    ASSERT_TRUE(queue.TryPop(task));
    queue.Finish(task.filename);
    for (int index = 0; index < 5; ++index) {
        ASSERT_TRUE(queue.Push(MakeTask(std::format("next-{}.mp4", index))));
        EXPECT_FALSE(queue.TryStartPump());
    }
    EXPECT_TRUE(queue.KeepPumpRunning());
    for (int index = 0; index < 5; ++index) {
        ASSERT_TRUE(queue.TryPop(task));
        EXPECT_EQ(task.filename, std::format("next-{}.mp4", index));
        queue.Finish(task.filename);
    }
    EXPECT_FALSE(queue.KeepPumpRunning());
}

TEST(RecordTransfer, AbortDoesNotRemoveDedupeState) {
    RecordFetchQueue queue;
    ASSERT_TRUE(queue.Push(MakeTask("a.mp4")));
    ASSERT_TRUE(queue.TryStartPump());
    queue.AbortPump();
    EXPECT_FALSE(queue.Push(MakeTask("a.mp4")));
    EXPECT_TRUE(queue.TryStartPump());
}

TEST(RecordTransfer, RequeueRequiresActiveOrQueuedNameButPreservesOrder) {
    RecordFetchQueue queue;
    auto first = MakeTask("first.mp4");
    auto second = MakeTask("second.mp4");
    ASSERT_TRUE(queue.Push(first));
    ASSERT_TRUE(queue.Push(second));
    ASSERT_TRUE(queue.TryStartPump());
    RecordFetchTask task;
    ASSERT_TRUE(queue.TryPop(task));
    ASSERT_EQ(task.filename, first.filename);
    ASSERT_TRUE(queue.Requeue(task));
    ASSERT_TRUE(queue.TryPop(task));
    EXPECT_EQ(task.filename, second.filename);
    ASSERT_TRUE(queue.TryPop(task));
    EXPECT_EQ(task.filename, first.filename);
}

TEST(RecordTransfer, ConcurrentUniquePushesRemainDrainable) {
    const auto queue = std::make_shared<RecordFetchQueue>();
    const auto accepted = std::make_shared<std::atomic_int>(0);
    std::vector<std::thread> producers;
    for (int index = 0; index < 64; ++index) {
        producers.emplace_back([queue, accepted, index]() {
            if (queue->Push(MakeTask(std::format("concurrent-{}.mp4", index)))) {
                ++(*accepted);
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    ASSERT_EQ(accepted->load(), 64);
    ASSERT_TRUE(queue->TryStartPump());
    RecordFetchTask task;
    int drained = 0;
    while (queue->TryPop(task)) {
        ++drained;
        queue->Finish(task.filename);
    }
    EXPECT_EQ(drained, 64);
    EXPECT_FALSE(queue->KeepPumpRunning());
}

TEST(RecordTransfer, ConcurrentDuplicatePushHasOneWinner) {
    const auto queue = std::make_shared<RecordFetchQueue>();
    const auto accepted = std::make_shared<std::atomic_int>(0);
    std::vector<std::thread> producers;
    for (int index = 0; index < 32; ++index) {
        producers.emplace_back([queue, accepted]() {
            if (queue->Push(MakeTask("same.mp4"))) {
                ++(*accepted);
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    EXPECT_EQ(accepted->load(), 1);
}

// ---- upload url parsing ----

TEST(RecordTransfer, ParseUploadUrlHttpWithPort) {
    bool ssl = true;
    std::string host, path;
    int port = 0;
    ASSERT_TRUE(ParseUploadUrl("http://10.0.0.8:30300/api/v1/record/upload", ssl, host, port, path));
    EXPECT_FALSE(ssl);
    EXPECT_EQ(host, "10.0.0.8");
    EXPECT_EQ(port, 30300);
    EXPECT_EQ(path, "/api/v1/record/upload");
}

TEST(RecordTransfer, ParseUploadUrlHttpsDefaultPort) {
    bool ssl = false;
    std::string host, path;
    int port = 0;
    ASSERT_TRUE(ParseUploadUrl("https://console.example.com/api/v1/record/upload", ssl, host, port, path));
    EXPECT_TRUE(ssl);
    EXPECT_EQ(host, "console.example.com");
    EXPECT_EQ(port, 443);
    EXPECT_EQ(path, "/api/v1/record/upload");
}

TEST(RecordTransfer, ParseUploadUrlHttpDefaultPortAndRootPath) {
    bool ssl = true;
    std::string host, path;
    int port = 0;
    ASSERT_TRUE(ParseUploadUrl("http://192.168.1.2", ssl, host, port, path));
    EXPECT_FALSE(ssl);
    EXPECT_EQ(host, "192.168.1.2");
    EXPECT_EQ(port, 80);
    EXPECT_EQ(path, "/");
}

TEST(RecordTransfer, ParseUploadUrlRejectsMalformed) {
    bool ssl = false;
    std::string host, path;
    int port = 0;
    EXPECT_FALSE(ParseUploadUrl("", ssl, host, port, path));
    EXPECT_FALSE(ParseUploadUrl("ftp://10.0.0.8/x", ssl, host, port, path));
    EXPECT_FALSE(ParseUploadUrl("http:///path", ssl, host, port, path));
    EXPECT_FALSE(ParseUploadUrl("http://10.0.0.8:/path", ssl, host, port, path));
    EXPECT_FALSE(ParseUploadUrl("http://10.0.0.8:abc/path", ssl, host, port, path));
    EXPECT_FALSE(ParseUploadUrl("http://10.0.0.8:70000/path", ssl, host, port, path));
}

// ---- file mtime helper ----

TEST(RecordTransfer, FileMtimeSeconds) {
    EXPECT_EQ(FileMtimeSeconds(std::filesystem::path("no_such_file_hopefully.mp4")), 0);

    const auto tmp = std::filesystem::temp_directory_path() / "px_record_transfer_mtime_test.tmp";
    {
        std::ofstream ofs(tmp);
        ofs << "x";
    }
    const int64_t mt = FileMtimeSeconds(tmp);
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    EXPECT_GT(mt, 0);
    EXPECT_NEAR(mt, now, 60);
    std::filesystem::remove(tmp);
}
