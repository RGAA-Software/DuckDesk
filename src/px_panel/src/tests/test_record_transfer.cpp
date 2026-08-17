//
// Created by RGAA on 2026/08/17.
//
// Unit tests for the record-fetch tunnel queue / retry / url parser
// (docs/cms_render_records_view_design.md section 6.2 / 7.2).
//

#include <gtest/gtest.h>

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

    RecordFetchTask out;
    ASSERT_TRUE(q.WaitPop(out));
    EXPECT_EQ(out.filename, "a.mp4");
    ASSERT_TRUE(q.WaitPop(out));
    EXPECT_EQ(out.filename, "b.mp4");
    ASSERT_TRUE(q.WaitPop(out));
    EXPECT_EQ(out.filename, "c.mp4");
}

TEST(RecordTransfer, DedupeSameFilenameWhileInFlight) {
    RecordFetchQueue q;
    ASSERT_TRUE(q.Push(MakeTask("a.mp4")));
    // same file queued again -> rejected
    EXPECT_FALSE(q.Push(MakeTask("a.mp4")));
    EXPECT_TRUE(q.Push(MakeTask("b.mp4")));

    // pop it: still in-flight, re-push must still be rejected
    RecordFetchTask out;
    ASSERT_TRUE(q.WaitPop(out));
    EXPECT_FALSE(q.Push(MakeTask("a.mp4")));

    // after Finish the name is free again
    q.Finish("a.mp4");
    EXPECT_TRUE(q.Push(MakeTask("a.mp4")));
}

TEST(RecordTransfer, RequeueKeepsDedupeAndAppendsAtTail) {
    RecordFetchQueue q;
    ASSERT_TRUE(q.Push(MakeTask("a.mp4")));
    ASSERT_TRUE(q.Push(MakeTask("b.mp4")));

    RecordFetchTask out;
    ASSERT_TRUE(q.WaitPop(out));
    EXPECT_EQ(out.filename, "a.mp4");
    out.attempt = 1;
    q.Requeue(out);                      // a goes to the tail, still deduped
    EXPECT_FALSE(q.Push(MakeTask("a.mp4")));

    ASSERT_TRUE(q.WaitPop(out));
    EXPECT_EQ(out.filename, "b.mp4");    // b comes before re-queued a
    ASSERT_TRUE(q.WaitPop(out));
    EXPECT_EQ(out.filename, "a.mp4");
    EXPECT_EQ(out.attempt, 1);
}

TEST(RecordTransfer, WaitPopUnblocksOnPush) {
    RecordFetchQueue q;
    std::vector<std::string> got;
    std::thread consumer([&]() {
        RecordFetchTask out;
        while (q.WaitPop(out)) {
            got.push_back(out.filename);
            q.Finish(out.filename);
        }
    });
    q.Push(MakeTask("x.mp4"));
    q.Push(MakeTask("y.mp4"));
    // wait until drained, then stop
    for (int i = 0; i < 100 && q.Size() > 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    q.Stop();
    consumer.join();
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], "x.mp4");
    EXPECT_EQ(got[1], "y.mp4");
}

TEST(RecordTransfer, StopRejectsNewPush) {
    RecordFetchQueue q;
    q.Stop();
    EXPECT_FALSE(q.Push(MakeTask("a.mp4")));
    RecordFetchTask out;
    EXPECT_FALSE(q.WaitPop(out));
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
    ASSERT_TRUE(ParseUploadUrl("https://cms.example.com/api/v1/record/upload", ssl, host, port, path));
    EXPECT_TRUE(ssl);
    EXPECT_EQ(host, "cms.example.com");
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
