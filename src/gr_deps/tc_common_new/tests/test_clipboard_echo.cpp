//
// Unit tests for tc_common_new/clipboard/clipboard_echo.h
//

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "tc_common_new/clipboard/clipboard_echo.h"

using namespace tc::clipboard;

TEST(ClipboardEchoFilterTest, DefaultState) {
    EchoFilter filter;
    EXPECT_TRUE(filter.GetRemoteEcho().empty());
    EXPECT_FALSE(filter.IsOutboundSuppressed());
    EXPECT_FALSE(filter.ShouldSkipOutbound("hello"));
}

TEST(ClipboardEchoFilterTest, RemoteEchoSkipsMatchingOutbound) {
    EchoFilter filter;
    filter.SetRemoteEcho("remote-content");
    EXPECT_TRUE(filter.ShouldSkipOutbound("remote-content"));
    EXPECT_FALSE(filter.ShouldSkipOutbound("local-content"));
}

TEST(ClipboardEchoFilterTest, SuppressBlocksAllOutbound) {
    EchoFilter filter;
    filter.BeginSuppressOutbound();
    EXPECT_TRUE(filter.IsOutboundSuppressed());
    EXPECT_TRUE(filter.ShouldSkipOutbound("anything"));
    filter.EndSuppressOutbound();
    EXPECT_FALSE(filter.IsOutboundSuppressed());
    EXPECT_FALSE(filter.ShouldSkipOutbound("anything"));
}

TEST(ClipboardEchoFilterTest, NestedSuppress) {
    EchoFilter filter;
    filter.BeginSuppressOutbound();
    filter.BeginSuppressOutbound();
    EXPECT_TRUE(filter.IsOutboundSuppressed());
    filter.EndSuppressOutbound();
    EXPECT_TRUE(filter.IsOutboundSuppressed());
    filter.EndSuppressOutbound();
    EXPECT_FALSE(filter.IsOutboundSuppressed());
}

TEST(ClipboardEchoFilterTest, SuppressOutboundGuardRaii) {
    EchoFilter filter;
    {
        SuppressOutboundGuard guard(filter);
        EXPECT_TRUE(filter.IsOutboundSuppressed());
    }
    EXPECT_FALSE(filter.IsOutboundSuppressed());
}

TEST(ClipboardEchoFilterTest, EchoOverridesAfterUpdate) {
    EchoFilter filter;
    filter.SetRemoteEcho("first");
    EXPECT_TRUE(filter.ShouldSkipOutbound("first"));
    filter.SetRemoteEcho("second");
    EXPECT_FALSE(filter.ShouldSkipOutbound("first"));
    EXPECT_TRUE(filter.ShouldSkipOutbound("second"));
}

TEST(ClipboardEchoFilterTest, SuppressTakesPrecedenceOverEcho) {
    EchoFilter filter;
    filter.SetRemoteEcho("echo");
    filter.BeginSuppressOutbound();
    EXPECT_TRUE(filter.ShouldSkipOutbound("unrelated"));
    filter.EndSuppressOutbound();
    EXPECT_FALSE(filter.ShouldSkipOutbound("unrelated"));
    EXPECT_TRUE(filter.ShouldSkipOutbound("echo"));
}

TEST(ClipboardEchoFilterTest, ConcurrentReadersWriters) {
    EchoFilter filter;
    constexpr int kThreads = 8;
    constexpr int kIterations = 500;
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kIterations; ++i) {
                if (t % 2 == 0) {
                    filter.SetRemoteEcho("thread-" + std::to_string(i % 17));
                } else {
                    (void)filter.GetRemoteEcho();
                    (void)filter.ShouldSkipOutbound("probe");
                }
                if (i % 11 == 0) {
                    filter.BeginSuppressOutbound();
                    filter.EndSuppressOutbound();
                }
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    EXPECT_GE(filter.GetRemoteEcho().size(), 0u);
}

TEST(ClipboardEchoFilterTest, EmptyEchoDoesNotSkipNonemptyText) {
    EchoFilter filter;
    filter.SetRemoteEcho("");
    EXPECT_FALSE(filter.ShouldSkipOutbound("local"));
    EXPECT_TRUE(filter.ShouldSkipOutbound(""));
}
