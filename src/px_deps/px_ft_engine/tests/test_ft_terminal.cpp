#include <gtest/gtest.h>

#include "ft_terminal.h"
#include "px_client_panel_message.pb.h"
#include "px_render_panel_message.pb.h"

namespace px::ft {
namespace {

TEST(FtTerminal, ClassifiesStableStatusAndReason) {
    const auto completed = ClassifyTerminal("");
    EXPECT_EQ(completed.status, "succeeded");
    EXPECT_EQ(completed.reason, "completed");
    EXPECT_TRUE(completed.success);
    EXPECT_FALSE(completed.resumable);

    const auto cancelled = ClassifyTerminal("cancel");
    EXPECT_EQ(cancelled.status, "cancelled");
    EXPECT_EQ(cancelled.reason, "user_cancelled");

    const auto skipped = ClassifyTerminal("skipped");
    EXPECT_EQ(skipped.status, "skipped");
    EXPECT_EQ(skipped.reason, "user_skipped");

    const auto integrity = ClassifyTerminal("file transfer SHA-256 mismatch");
    EXPECT_EQ(integrity.status, "failed");
    EXPECT_EQ(integrity.reason, "integrity_mismatch");
    EXPECT_TRUE(integrity.resumable);

    const auto sequence = ClassifyTerminal(
        "file transfer block sequence mismatch: expected 2, got 3");
    EXPECT_EQ(sequence.reason, "block_sequence_mismatch");
    EXPECT_TRUE(sequence.resumable);

    const auto disconnected = ClassifyTerminal("FT transport disconnected");
    EXPECT_EQ(disconnected.status, "aborted");
    EXPECT_EQ(disconnected.reason, "transport_disconnected");
    EXPECT_TRUE(disconnected.resumable);
}

TEST(FtTerminal, ClassifiesPermissionPathLimitIoAndFallback) {
    EXPECT_EQ(ClassifyTerminal("No permission of file transfer").reason,
              "permission_denied");
    EXPECT_EQ(ClassifyTerminal("Path not exists").reason, "source_not_found");
    EXPECT_EQ(ClassifyTerminal("Too many files").reason, "file_count_limit");
    EXPECT_EQ(ClassifyTerminal("file write failed").reason, "io_error");
    EXPECT_EQ(ClassifyTerminal("unexpected peer error").reason, "transfer_failed");
}

TEST(FtTerminal, ClientPanelProtoPreservesTerminalFields) {
    pxcp::CpMessage outgoing;
    outgoing.set_type(pxcp::kCpFileTransferEnd);
    auto& terminal = *outgoing.mutable_ft_transfer_end();
    terminal.set_the_file_id("client-job");
    terminal.set_success(false);
    terminal.set_status("failed");
    terminal.set_end_reason("integrity_mismatch");

    pxcp::CpMessage incoming;
    ASSERT_TRUE(incoming.ParseFromString(outgoing.SerializeAsString()));
    EXPECT_EQ(incoming.ft_transfer_end().status(), "failed");
    EXPECT_EQ(incoming.ft_transfer_end().end_reason(), "integrity_mismatch");
}

TEST(FtTerminal, RenderPanelProtoPreservesTerminalFields) {
    pxrp::RpMessage outgoing;
    outgoing.set_type(pxrp::kRpFileTransferEnd);
    auto& terminal = *outgoing.mutable_ft_end();
    terminal.set_the_file_id("render-job");
    terminal.set_success(false);
    terminal.set_status("aborted");
    terminal.set_end_reason("transport_disconnected");

    pxrp::RpMessage incoming;
    ASSERT_TRUE(incoming.ParseFromString(outgoing.SerializeAsString()));
    EXPECT_EQ(incoming.ft_end().status(), "aborted");
    EXPECT_EQ(incoming.ft_end().end_reason(), "transport_disconnected");
}

TEST(FtTerminal, LegacyProtoWithoutTerminalFieldsRemainsCompatible) {
    pxcp::CpMessage client;
    client.set_type(pxcp::kCpFileTransferEnd);
    client.mutable_ft_transfer_end()->set_success(true);
    pxcp::CpMessage parsed_client;
    ASSERT_TRUE(parsed_client.ParseFromString(client.SerializeAsString()));
    EXPECT_TRUE(parsed_client.ft_transfer_end().status().empty());
    EXPECT_TRUE(parsed_client.ft_transfer_end().end_reason().empty());

    pxrp::RpMessage render;
    render.set_type(pxrp::kRpFileTransferEnd);
    render.mutable_ft_end()->set_success(false);
    pxrp::RpMessage parsed_render;
    ASSERT_TRUE(parsed_render.ParseFromString(render.SerializeAsString()));
    EXPECT_TRUE(parsed_render.ft_end().status().empty());
    EXPECT_TRUE(parsed_render.ft_end().end_reason().empty());
}

} // namespace
} // namespace px::ft
