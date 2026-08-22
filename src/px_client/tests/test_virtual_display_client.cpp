#include <gtest/gtest.h>

#include "ct_virtual_display_protocol.h"
#include "ui/virtual_display_ui_state.h"

namespace px {

    TEST(VirtualDisplayUiStateTest, StartsDisabledAndNormalizesMaximum) {
        VirtualDisplayUiState state;

        EXPECT_FALSE(state.CanAdd());
        EXPECT_FALSE(state.CanRemove());
        EXPECT_EQ(state.Maximum(), 2U);

        state.ApplyStatus(true, 0, 0, 7);
        EXPECT_TRUE(state.CanAdd());
        EXPECT_FALSE(state.CanRemove());
        EXPECT_EQ(state.Maximum(), 2U);
        EXPECT_EQ(state.Generation(), 7U);
    }

    TEST(VirtualDisplayUiStateTest, CreateRequestBlocksDuplicateOperationsUntilSuccess) {
        VirtualDisplayUiState state;
        state.ApplyStatus(true, 0, 2, 1);

        EXPECT_TRUE(state.BeginRequest(VirtualDisplayUiOperation::kCreate, "native-1"));
        EXPECT_TRUE(state.IsBusy());
        EXPECT_FALSE(state.CanAdd());
        EXPECT_FALSE(state.CanRemove());
        EXPECT_FALSE(state.BeginRequest(VirtualDisplayUiOperation::kCreate, "native-2"));

        EXPECT_EQ(state.ApplyResult("native-1", true, false, 1, 2, 2),
                  VirtualDisplayUiResultEffect::kCompleted);
        EXPECT_FALSE(state.IsBusy());
        EXPECT_TRUE(state.CanAdd());
        EXPECT_TRUE(state.CanRemove());
    }

    TEST(VirtualDisplayUiStateTest, FailedMatchingRequestRestoresControls) {
        VirtualDisplayUiState state;
        state.ApplyStatus(true, 1, 2, 3);
        ASSERT_TRUE(state.BeginRequest(VirtualDisplayUiOperation::kRemoveLast, "remove-1"));

        EXPECT_EQ(state.ApplyResult("remove-1", false, false, 1, 2, 3),
                  VirtualDisplayUiResultEffect::kFailed);
        EXPECT_FALSE(state.IsBusy());
        EXPECT_TRUE(state.CanAdd());
        EXPECT_TRUE(state.CanRemove());
    }

    TEST(VirtualDisplayUiStateTest, ReconnectWaitsForExpectedTopologyStatus) {
        VirtualDisplayUiState state;
        state.ApplyStatus(true, 0, 2, 10);
        ASSERT_TRUE(state.BeginRequest(VirtualDisplayUiOperation::kCreate, "create-1"));

        EXPECT_EQ(state.ApplyResult("create-1", true, true, 1, 2, 11),
                  VirtualDisplayUiResultEffect::kAwaitingReconnect);
        EXPECT_EQ(state.Phase(), VirtualDisplayUiPhase::kReconnecting);

        state.ApplyStatus(true, 0, 2, 10);
        EXPECT_TRUE(state.IsBusy());
        state.ApplyStatus(true, 1, 2, 10);
        EXPECT_TRUE(state.IsBusy());
        state.ApplyStatus(true, 1, 2, 11);
        EXPECT_FALSE(state.IsBusy());
        EXPECT_TRUE(state.CanRemove());
    }

    TEST(VirtualDisplayUiStateTest, SuccessfulNetworkReconnectCompletesPendingTopologyChange) {
        VirtualDisplayUiState state;
        state.ApplyStatus(true, 1, 2, 20);
        ASSERT_TRUE(state.BeginRequest(VirtualDisplayUiOperation::kRemoveLast, "remove-2"));
        ASSERT_EQ(state.ApplyResult("remove-2", true, true, 0, 2, 21),
                  VirtualDisplayUiResultEffect::kAwaitingReconnect);

        EXPECT_TRUE(state.CompleteReconnect());
        EXPECT_FALSE(state.IsBusy());
        EXPECT_TRUE(state.CanAdd());
        EXPECT_FALSE(state.CompleteReconnect());
    }

    TEST(VirtualDisplayUiStateTest, UnrelatedResultCannotCompleteThisPanelRequest) {
        VirtualDisplayUiState state;
        state.ApplyStatus(true, 0, 2, 1);
        ASSERT_TRUE(state.BeginRequest(VirtualDisplayUiOperation::kCreate, "ours"));

        EXPECT_EQ(state.ApplyResult("another-window", true, false, 1, 2, 2),
                  VirtualDisplayUiResultEffect::kIgnored);
        EXPECT_TRUE(state.IsBusy());
        EXPECT_EQ(state.Owned(), 1U);
        EXPECT_FALSE(state.Timeout("another-window"));
        EXPECT_TRUE(state.Timeout("ours"));
        EXPECT_FALSE(state.IsBusy());
    }

    TEST(VirtualDisplayUiStateTest, FeatureDisableCancelsPendingRequest) {
        VirtualDisplayUiState state;
        state.ApplyStatus(true, 1, 2, 1);
        ASSERT_TRUE(state.BeginRequest(VirtualDisplayUiOperation::kRemoveLast, "remove-1"));

        state.ApplyStatus(false, 1, 2, 1);
        EXPECT_FALSE(state.IsBusy());
        EXPECT_FALSE(state.CanAdd());
        EXPECT_FALSE(state.CanRemove());
    }

    TEST(VirtualDisplayUiStateTest, OperationGuardsMatchOwnedDisplayCount) {
        VirtualDisplayUiState state;
        state.ApplyStatus(true, 0, 2, 1);
        EXPECT_FALSE(state.BeginRequest(VirtualDisplayUiOperation::kRemoveLast, "remove-empty"));

        state.ApplyStatus(true, 2, 2, 2);
        EXPECT_FALSE(state.BeginRequest(VirtualDisplayUiOperation::kCreate, "create-full"));
        EXPECT_TRUE(state.BeginRequest(VirtualDisplayUiOperation::kRemoveLast, "remove-full"));
    }

    TEST(VirtualDisplayProtocolTest, BuildsCreateRequestWithConnectionIdentityAndMode) {
        const auto message = MakeVirtualDisplayRequestMessage(
            "device-90", "stream-native", "native-42", kRemoteVirtualDisplayCreate,
            2560, 1440, 75);

        EXPECT_EQ(message.type(), kVirtualDisplayRequest);
        EXPECT_EQ(message.device_id(), "device-90");
        EXPECT_EQ(message.stream_id(), "stream-native");
        ASSERT_TRUE(message.has_virtual_display_request());
        const auto& request = message.virtual_display_request();
        EXPECT_EQ(request.request_id(), "native-42");
        EXPECT_EQ(request.operation(), kRemoteVirtualDisplayCreate);
        EXPECT_EQ(request.width(), 2560U);
        EXPECT_EQ(request.height(), 1440U);
        EXPECT_EQ(request.refresh_hz(), 75U);
    }

    TEST(VirtualDisplayProtocolTest, RequestIdsAreUniqueAcrossNativePanels) {
        const auto first = NextNativeVirtualDisplayRequestId(1234);
        const auto second = NextNativeVirtualDisplayRequestId(1234);

        EXPECT_NE(first, second);
        EXPECT_EQ(first.find("native-1234-"), 0U);
        EXPECT_EQ(second.find("native-1234-"), 0U);
    }

    TEST(VirtualDisplayProtocolTest, RoundTripsRemoveRequestThroughProtobuf) {
        const auto original = MakeVirtualDisplayRequestMessage(
            "device", "stream", "remove-9", kRemoteVirtualDisplayRemoveLast,
            1920, 1080, 60);
        const auto bytes = original.SerializeAsString();
        Message parsed;

        ASSERT_TRUE(parsed.ParseFromString(bytes));
        EXPECT_EQ(parsed.virtual_display_request().request_id(), "remove-9");
        EXPECT_EQ(parsed.virtual_display_request().operation(), kRemoteVirtualDisplayRemoveLast);
    }

}
