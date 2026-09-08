#include "sdk_clipboard_protocol.h"
#include <memory>

#include <gtest/gtest.h>

namespace px {
namespace {

ClipboardRespBuffer Response(const ClipboardReadRequest& request, std::string bytes = "data") {
    ClipboardRespBuffer response{};
    response.set_full_name(request.transfer_name);
    response.set_req_index(request.index);
    response.set_req_start(request.offset);
    response.set_req_size(request.size);
    response.set_read_size(static_cast<std::int64_t>(bytes.size()));
    response.set_buffer(std::move(bytes));
    return response;
}

TEST(SdkClipboardProtocol, ValidatesSignedRangesAndMessageBudget) {
    EXPECT_TRUE((ClipboardReadRequest{"file", 0, 0, kClipboardReadChunkBytes}.IsValid()));
    EXPECT_FALSE((ClipboardReadRequest{"file", -1, 0, 1}.IsValid()));
    EXPECT_FALSE((ClipboardReadRequest{"file", 0, -1, 1}.IsValid()));
    EXPECT_FALSE((ClipboardReadRequest{"file", 0, 0, 0}.IsValid()));
    EXPECT_FALSE((ClipboardReadRequest{"file", 0, 0, kClipboardReadChunkBytes + 1}.IsValid()));
    EXPECT_FALSE((ClipboardReadRequest{"file", 0, std::numeric_limits<std::int64_t>::max(), 1}.IsValid()));
    EXPECT_FALSE(IsClipboardTransferNameValid(std::string("a\0b", 3)));
    EXPECT_FALSE(IsClipboardTransferNameValid(std::string(4097, 'a')));
    EXPECT_FALSE(IsClipboardFileDescriptorValid({}, "file", 1));
    EXPECT_FALSE(IsClipboardFileDescriptorValid("name", "file", -1));
    EXPECT_TRUE(IsClipboardFileDescriptorValid("name", "file", 0));
}

TEST(SdkClipboardProtocol, RejectsEveryMismatchedResponseField) {
    const ClipboardReadRequest request{"file", 2, 7, 8};
    EXPECT_TRUE(request.Matches(Response(request)));
    auto response = Response(request);
    response.set_full_name("other");
    EXPECT_FALSE(request.Matches(response));
    response = Response(request);
    response.set_req_index(1);
    EXPECT_FALSE(request.Matches(response));
    response = Response(request);
    response.set_req_start(0);
    EXPECT_FALSE(request.Matches(response));
    response = Response(request);
    response.set_req_size(9);
    EXPECT_FALSE(request.Matches(response));
    response = Response(request);
    response.set_read_size(-1);
    EXPECT_FALSE(request.Matches(response));
    response = Response(request);
    response.set_read_size(3);
    EXPECT_FALSE(request.Matches(response));
    EXPECT_FALSE(request.Matches(Response(request, std::string(9, 'x'))));
    EXPECT_TRUE(request.Matches(Response(request, {})));
}

TEST(SdkClipboardProtocol, CapsGrowingFileAndAllowsExactEofOnly) {
    const ClipboardReadRequest request{"file", 0, 7, 8};
    EXPECT_EQ(request.ReadSize(10), 3);
    EXPECT_EQ(request.ReadSize(7), 0);
    EXPECT_FALSE(request.ReadSize(6));
    EXPECT_FALSE(request.ReadSize(-1));
}

TEST(SdkClipboardProtocol, CancellationAndFailedSendNeverReuseIdentity) {
    ClipboardPendingRead pending{};
    const auto first = pending.Begin("file", 0, 8);
    ASSERT_TRUE(first);
    const auto old = Response(*first);
    EXPECT_TRUE(pending.Accepts(old));
    pending.Cancel();
    pending.Cancel();
    EXPECT_FALSE(pending.Accepts(old));
    const auto retried = pending.Begin("file", 0, 8);
    ASSERT_TRUE(retried);
    EXPECT_GT(retried->index, first->index);
    EXPECT_FALSE(pending.Accepts(old));
    EXPECT_TRUE(pending.Accepts(Response(*retried)));
    EXPECT_FALSE(pending.Begin("file", -1, 8));
    EXPECT_FALSE(pending.Accepts(Response(*retried)));
}

TEST(SdkClipboardProtocol, RepeatedGenerationsRejectPriorOfferEvenWithMatchingSequence) {
    for (int index{}; index < 10; ++index) {
        ClipboardPendingRead first{};
        ClipboardPendingRead next{};
        const auto old = first.Begin("pixels-clipboard://old/0", 0, 8);
        const auto current = next.Begin("pixels-clipboard://current/0", 0, 8);
        ASSERT_TRUE(old);
        ASSERT_TRUE(current);
        EXPECT_EQ(old->index, current->index);
        EXPECT_FALSE(next.Accepts(Response(*old)));
        EXPECT_TRUE(next.Accepts(Response(*current)));
    }
}

TEST(SdkClipboardProtocol, QueuedUpdateCannotOverwriteLaterClipboardOrOutliveOwner) {
    const auto epoch = std::make_shared<ClipboardUpdateEpoch>();
    const auto weak = std::weak_ptr(epoch);
    const auto old = epoch->Advance();
    const auto queued = [weak, old] {
        const auto owner = weak.lock();
        return owner && owner->IsCurrent(old);
    };
    EXPECT_TRUE(queued());
    const auto replacement = epoch->Advance();
    EXPECT_FALSE(queued());
    EXPECT_TRUE(epoch->IsCurrent(replacement));
    static_cast<void>(epoch->Advance()); // Stop invalidates all pending publications.
    EXPECT_FALSE(epoch->IsCurrent(replacement));
    EXPECT_FALSE(epoch->IsCurrent(0));
    auto transient = std::make_shared<ClipboardUpdateEpoch>();
    const auto generation = transient->Advance();
    const auto expired = [owner = std::weak_ptr(transient), generation] {
        const auto locked = owner.lock();
        return locked && locked->IsCurrent(generation);
    };
    transient.reset();
    EXPECT_FALSE(expired());
}

} // namespace
} // namespace px
