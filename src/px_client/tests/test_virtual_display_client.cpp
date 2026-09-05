#include <gtest/gtest.h>

#include "ct_virtual_display_protocol.h"
#include "render_view_capacity.h"
#include "px_common/rtc_monitor_track_slots.h"
#include "px_render/architecture/encoders/ffmpeg/ffmpeg_encoder_defs.h"
#include "ui/virtual_display_ui_state.h"
#include "px_common/virtual_display_timeouts.h"
#include "px_common/rtc_signal_identity.h"

namespace px {

    TEST(RtcSignalIdentityTest, DesktopFallsBackToHistoricalServerPrefix) {
        EXPECT_EQ(ResolveRtcSignalRemoteDeviceId("001190520", ""),
                  "server_001190520");
        EXPECT_EQ(ResolveRtcFileTransferSignalRemoteDeviceId("001190520", ""),
                  "ft_server_001190520");
    }

    TEST(RtcSignalIdentityTest, CloudApplicationUsesConsoleInstanceIdentityExactly) {
        const std::string signal_id =
            "server_001190520__instance__inst-11-a204a9c4";
        EXPECT_EQ(ResolveRtcSignalRemoteDeviceId("001190520", signal_id), signal_id);
        EXPECT_EQ(ResolveRtcFileTransferSignalRemoteDeviceId(
                      "001190520", signal_id),
                  "ft_server_001190520__instance__inst-11-a204a9c4");
    }

    TEST(VirtualDisplayTimeoutTest, BudgetsCoverEachSlowerDownstreamPhase) {
        EXPECT_GT(kVirtualDisplayQueryRenderTimeout, kVirtualDisplayQueryServiceTimeout);
        EXPECT_GT(kVirtualDisplayMutationRenderTimeout, kVirtualDisplayMutationServiceTimeout);
        EXPECT_GT(kVirtualDisplayResetRenderTimeout, kVirtualDisplayResetServiceTimeout);
        EXPECT_GT(kVirtualDisplayClientOperationTimeout,
                  kVirtualDisplayMutationRenderTimeout + kVirtualDisplayCaptureRebuildTimeout);
    }

    TEST(RenderViewCapacityTest, StartsWithOnlyTheMainView) {
        EXPECT_EQ(ResolveRequiredRenderViewCount(0, 4), 1U);
        EXPECT_EQ(ResolveRequiredRenderViewCount(1, 4), 1U);
    }

    TEST(RenderViewCapacityTest, ExpandsToTheReportedMonitorCount) {
        EXPECT_EQ(ResolveRequiredRenderViewCount(2, 4), 2U);
        EXPECT_EQ(ResolveRequiredRenderViewCount(4, 4), 4U);
    }

    TEST(RenderViewCapacityTest, HonorsConfiguredAndHardLimits) {
        EXPECT_EQ(ResolveRequiredRenderViewCount(8, 4), 4U);
        EXPECT_EQ(ResolveRequiredRenderViewCount(20, 20), 8U);
        EXPECT_EQ(ResolveRequiredRenderViewCount(2, 0), 1U);
    }

    TEST(RtcMonitorTrackSlotsTest, ReservesEightNegotiatedTracks) {
        EXPECT_EQ(kReservedRtcMonitorTrackCount, 8);

        std::vector<std::string> slots(kReservedRtcMonitorTrackCount);
        const std::vector<std::string> monitors {
            "DISPLAY1", "DISPLAY2", "DISPLAY3", "DISPLAY4",
            "DISPLAY5", "DISPLAY6", "DISPLAY7", "DISPLAY8",
        };
        EXPECT_TRUE(ReconcileRtcMonitorTrackSlots(slots, monitors));
        EXPECT_EQ(slots, monitors);
    }

    TEST(VirtualDisplayUiStateTest, StartsDisabledAndNormalizesMaximum) {
        VirtualDisplayUiState state;

        EXPECT_FALSE(state.CanAdd());
        EXPECT_FALSE(state.CanRemove());
        EXPECT_EQ(state.Maximum(), kVirtualDisplayMaximumCount);

        state.ApplyStatus(true, 0, 0, 7);
        EXPECT_TRUE(state.CanAdd());
        EXPECT_FALSE(state.CanRemove());
        EXPECT_EQ(state.Maximum(), kVirtualDisplayMaximumCount);
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

    TEST(VirtualDisplayProtocolTest, NativeClientKeepsHealthyTransportForTopologyChange) {
        EXPECT_EQ(NormalizeNativeVirtualDisplayResponseState(
                      true, kVirtualDisplayNeedReconnect),
                  kVirtualDisplayReady);
        EXPECT_EQ(NormalizeNativeVirtualDisplayResponseState(
                      true, kVirtualDisplayReady),
                  kVirtualDisplayReady);
    }

    TEST(VirtualDisplayProtocolTest, NativeClientPreservesFailureState) {
        EXPECT_EQ(NormalizeNativeVirtualDisplayResponseState(
                      false, kVirtualDisplayFailed),
                  kVirtualDisplayFailed);
        EXPECT_EQ(NormalizeNativeVirtualDisplayResponseState(
                      false, kVirtualDisplayNeedReconnect),
                  kVirtualDisplayNeedReconnect);
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

    TEST(RtcMonitorTrackSlotsTest, HotAddUsesFirstNegotiatedEmptySlot) {
        std::vector<std::string> slots { "DISPLAY1", "", "", "" };

        EXPECT_TRUE(ReconcileRtcMonitorTrackSlots(slots, { "DISPLAY1", "DISPLAY44" }));
        EXPECT_EQ(slots, (std::vector<std::string> { "DISPLAY1", "DISPLAY44", "", "" }));
    }

    TEST(RtcMonitorTrackSlotsTest, ObservedInnerCaptureClaimsSlotWhenTopologyIsEmpty) {
        std::vector<std::string> slots { "", "", "", "" };
        std::vector<std::string> active_monitors;

        IncludeObservedRtcMonitor(active_monitors, "webview");

        ASSERT_EQ(active_monitors, (std::vector<std::string> { "webview" }));
        EXPECT_TRUE(ReconcileRtcMonitorTrackSlots(slots, active_monitors));
        EXPECT_EQ(slots, (std::vector<std::string> { "webview", "", "", "" }));
    }

    TEST(RtcMonitorTrackSlotsTest, ObservedMonitorDoesNotDuplicateEnumeratedTopology) {
        std::vector<std::string> active_monitors { "DISPLAY1", "webview" };

        IncludeObservedRtcMonitor(active_monitors, "webview");
        IncludeObservedRtcMonitor(active_monitors, "");

        EXPECT_EQ(active_monitors,
                  (std::vector<std::string> { "DISPLAY1", "webview" }));
    }

    TEST(RtcEncoderKeyframePolicyTest, ForcesJoinSafeIdrForSoftwareAndNvencH264) {
        EXPECT_TRUE(ShouldEnableForcedH264Idr("libx264"));
        EXPECT_TRUE(ShouldEnableForcedH264Idr("h264_nvenc"));
        EXPECT_FALSE(ShouldEnableForcedH264Idr("h264_qsv"));
        EXPECT_FALSE(ShouldEnableForcedH264Idr("hevc_nvenc"));
    }

    TEST(RtcMonitorTrackSlotsTest, RemovalReleasesSlotWithoutMovingActiveTracks) {
        std::vector<std::string> slots { "DISPLAY1", "DISPLAY44", "", "" };

        EXPECT_TRUE(ReconcileRtcMonitorTrackSlots(slots, { "DISPLAY1" }));
        EXPECT_EQ(slots, (std::vector<std::string> { "DISPLAY1", "", "", "" }));
    }

    TEST(RtcMonitorTrackSlotsTest, ActiveAssignmentsSurviveEnumerationReorder) {
        std::vector<std::string> slots { "DISPLAY1", "DISPLAY44", "", "" };

        EXPECT_FALSE(ReconcileRtcMonitorTrackSlots(slots, { "DISPLAY44", "DISPLAY1" }));
        EXPECT_EQ(slots, (std::vector<std::string> { "DISPLAY1", "DISPLAY44", "", "" }));
    }

    TEST(RtcMonitorTrackSlotsTest, ReplacementReusesReleasedSlot) {
        std::vector<std::string> slots { "DISPLAY1", "DISPLAY44", "DISPLAY45", "" };

        EXPECT_TRUE(ReconcileRtcMonitorTrackSlots(
            slots, { "DISPLAY1", "DISPLAY45", "DISPLAY46" }));
        EXPECT_EQ(slots, (std::vector<std::string> {
            "DISPLAY1", "DISPLAY46", "DISPLAY45", "" }));
    }

    TEST(RtcMonitorTrackSlotsTest, NegotiatedCapacityIsNeverExceeded) {
        std::vector<std::string> slots { "DISPLAY1", "DISPLAY2" };

        EXPECT_FALSE(ReconcileRtcMonitorTrackSlots(
            slots, { "DISPLAY1", "DISPLAY2", "DISPLAY3" }));
        EXPECT_EQ(slots, (std::vector<std::string> { "DISPLAY1", "DISPLAY2" }));
    }

    TEST(RtcMonitorTrackSlotsTest, FrameSequenceAcceptsFirstFrameAndForwardGaps) {
        RtcFrameSequenceState state;

        EXPECT_EQ(AdvanceRtcFrameSequence(state, 42).disposition_,
                  RtcFrameSequenceDisposition::kFirstFrame);
        EXPECT_EQ(AdvanceRtcFrameSequence(state, 43).disposition_,
                  RtcFrameSequenceDisposition::kConsecutive);
        const auto gap = AdvanceRtcFrameSequence(state, 46);
        EXPECT_EQ(gap.disposition_, RtcFrameSequenceDisposition::kForwardGap);
        EXPECT_EQ(gap.gap_, 3U);
    }

    TEST(RtcMonitorTrackSlotsTest, FrameSequenceTreatsBackwardValueAsStreamReset) {
        RtcFrameSequenceState state;
        AdvanceRtcFrameSequence(state, 900);

        const auto reset = AdvanceRtcFrameSequence(state, 1);
        EXPECT_EQ(reset.disposition_, RtcFrameSequenceDisposition::kReset);
        EXPECT_EQ(reset.gap_, 0U);
        EXPECT_EQ(state.last_frame_index_, 1U);
    }

    TEST(RtcMonitorTrackSlotsTest, FrameSequenceTreatsRepeatedValueAsStreamReset) {
        RtcFrameSequenceState state;
        AdvanceRtcFrameSequence(state, 0);

        const auto reset = AdvanceRtcFrameSequence(state, 0);
        EXPECT_EQ(reset.disposition_, RtcFrameSequenceDisposition::kReset);
        EXPECT_TRUE(state.initialized_);
    }

    TEST(RtcMonitorTrackSlotsTest, TopologyRebindForcesStreamResetBeforeFrameIndexRollsOver) {
        RtcFrameSequenceState state;
        AdvanceRtcFrameSequence(state, 1);
        const auto next_frame = AdvanceRtcFrameSequence(state, 2);

        EXPECT_EQ(next_frame.disposition_, RtcFrameSequenceDisposition::kConsecutive);
        EXPECT_TRUE(ShouldResetRtcCaptureStream(true, next_frame));
        EXPECT_FALSE(ShouldResetRtcCaptureStream(false, next_frame));
    }

}
