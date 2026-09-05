#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "px_common/data.h"
#include "px_capture/capture_message.h"
#include "px_render/architecture/encoders/video_encoder_module.h"
#include "px_render/architecture/extensions/flow_node_plugin.h"
#include "px_render/architecture/modules/render_module.h"
#include "px_render/architecture/encoders/amf/amf_video_encoder.h"
#include "px_render/architecture/encoders/ffmpeg/ffmpeg_video_encoder.h"
#include "px_render/architecture/encoders/nvenc/nvenc_encoder_module.h"
#include "px_render/architecture/sources/dda/dda_capture_source.h"
#include "px_render/architecture/sources/gdi/gdi_capture_source.h"
#include "px_render/network/relay/relay_transport.h"
#include "px_render/network/udp/udp_transport.h"
#include "px_render/network/ws/ws_transport.h"

namespace px {
namespace {

TEST(RenderBuiltinLinkageTest, ProductionModulesHaveStaticConstructorsAndIds) {
    const std::vector<std::shared_ptr<RenderModule>> modules{
        std::make_shared<DdaCaptureSource>(),
        std::make_shared<GdiCaptureSource>(),
        std::make_shared<FfmpegVideoEncoder>(),
        std::make_shared<AmfVideoEncoder>(),
        std::make_shared<NvencEncoderModule>(),
        std::make_shared<WsTransport>(),
        std::make_shared<UdpTransport>(),
        std::make_shared<RelayTransport>(),
    };
    for (const auto& module : modules) {
        ASSERT_TRUE(module);
        EXPECT_FALSE(module->Id().empty());
        EXPECT_FALSE(module->Name().empty());
    }

    static_assert(std::is_base_of_v<RenderModule, DdaCaptureSource>);
    static_assert(std::is_base_of_v<RenderModule, GdiCaptureSource>);
    static_assert(std::is_base_of_v<RenderModule, FfmpegVideoEncoder>);
    static_assert(std::is_base_of_v<RenderModule, AmfVideoEncoder>);
    static_assert(std::is_base_of_v<RenderModule, NvencEncoderModule>);
    static_assert(std::is_base_of_v<RenderModule, WsTransport>);
    static_assert(std::is_base_of_v<RenderModule, UdpTransport>);
    static_assert(std::is_base_of_v<RenderModule, RelayTransport>);
}

TEST(RenderBuiltinLinkageTest, VideoEncoderMetadataContractIsStronglyTyped) {
    using CpuResult = decltype(std::declval<VideoEncoderModule&>().Encode(
        std::declval<const std::shared_ptr<Image>&>(),
        std::uint64_t{}, std::declval<const CaptureVideoFrame&>()));
    using TextureResult = decltype(std::declval<VideoEncoderModule&>().Encode(
        std::declval<const Microsoft::WRL::ComPtr<ID3D11Texture2D>&>(),
        std::uint64_t{}, std::declval<const CaptureVideoFrame&>()));
    static_assert(std::is_same_v<CpuResult, VideoEncoderError>);
    static_assert(std::is_same_v<TextureResult, VideoEncoderError>);
}

TEST(RenderBuiltinLinkageTest, ExtensionContractsAreFlowNodeRolesNotLegacyPlugins) {
    static_assert(std::is_base_of_v<render::FlowNodePlugin, render::VideoSourcePlugin>);
    static_assert(std::is_base_of_v<render::FlowNodePlugin, render::AudioSourcePlugin>);
    static_assert(std::is_base_of_v<render::FlowNodePlugin, render::VideoProcessorPlugin>);
    static_assert(std::is_base_of_v<render::FlowNodePlugin, render::AudioProcessorPlugin>);
    static_assert(std::is_base_of_v<render::FlowNodePlugin, render::VideoEncoderPlugin>);
    static_assert(std::is_base_of_v<render::FlowNodePlugin, render::AudioEncoderPlugin>);
    static_assert(std::is_base_of_v<render::FlowNodePlugin, render::ObserverPlugin>);
    static_assert(std::is_base_of_v<render::FlowNodePlugin, render::SinkPlugin>);
    static_assert(!std::is_base_of_v<RenderModule, render::FlowNodePlugin>);
}

struct NetworkServiceProbe final {
    int network_broadcasts = 0;
    int file_transfer_broadcasts = 0;
    int rtc_allocations = 0;
    int udp_updates = 0;
};

TEST(RenderBuiltinLinkageTest, WsUsesExplicitNetworkCapabilitiesWithWeakLifetime) {
    const auto ws = std::make_shared<WsTransport>();
    auto probe = std::make_shared<NetworkServiceProbe>();
    const std::weak_ptr<NetworkServiceProbe> weak_probe = probe;

    ws->ConfigureNetworkServices(
        [weak_probe](const std::shared_ptr<Data>&, bool) {
            if (const auto state = weak_probe.lock()) {
                ++state->network_broadcasts;
            }
        },
        [weak_probe](const std::string&, const std::shared_ptr<Data>&, bool) {
            if (const auto state = weak_probe.lock()) {
                ++state->file_transfer_broadcasts;
            }
        },
        [weak_probe](const std::shared_ptr<PxLocalRtcRequestInfo>&,
                     WsTransport::LocalRtcCompletion completion) {
            if (const auto state = weak_probe.lock()) {
                ++state->rtc_allocations;
                completion(std::make_shared<PxLocalRtcReplyInfo>());
                return PxLocalRtcAllocResult::kOk;
            }
            return PxLocalRtcAllocResult::kFailed;
        },
        [weak_probe](const UdpMediaAssociation&) {
            if (const auto state = weak_probe.lock()) {
                ++state->udp_updates;
                return true;
            }
            return false;
        });

    const auto payload = Data::From("capability-payload");
    const auto rtc_request = std::make_shared<PxLocalRtcRequestInfo>();
    const auto rtc_reply_received = std::make_shared<bool>(false);

    for (int round = 0; round < 100; ++round) {
        ws->BroadcastNetworkMessage(payload, false);
        ws->BroadcastFileTransferMessage("stream", payload, true);
        EXPECT_EQ(ws->AllocateLocalRtcInstance(
                      rtc_request,
                      [rtc_reply_received](
                          const std::shared_ptr<PxLocalRtcReplyInfo>& reply) {
                          *rtc_reply_received = static_cast<bool>(reply);
                      }),
                  PxLocalRtcAllocResult::kOk);
        EXPECT_TRUE(ws->UpdateUdpAssociation(UdpMediaAssociation{}));
    }
    EXPECT_EQ(probe->network_broadcasts, 100);
    EXPECT_EQ(probe->file_transfer_broadcasts, 100);
    EXPECT_EQ(probe->rtc_allocations, 100);
    EXPECT_EQ(probe->udp_updates, 100);
    EXPECT_TRUE(*rtc_reply_received);
    EXPECT_TRUE(ws->HasLocalRtcService());

    probe.reset();
    ws->BroadcastNetworkMessage(payload, false);
    ws->BroadcastFileTransferMessage("stream", payload, true);
    EXPECT_EQ(ws->AllocateLocalRtcInstance(rtc_request, {}),
              PxLocalRtcAllocResult::kFailed);
    EXPECT_FALSE(ws->UpdateUdpAssociation(UdpMediaAssociation{}));
}

struct IpcMediaIngressProbe final {
    std::uint64_t video_frame_index = 0;
    std::uint64_t audio_frame_index = 0;
};

TEST(RenderBuiltinLinkageTest, WsIpcMediaUsesTypedWeakIngress) {
    const auto ws = std::make_shared<WsTransport>();
    auto probe = std::make_shared<IpcMediaIngressProbe>();
    const std::weak_ptr<IpcMediaIngressProbe> weak_probe = probe;
    ws->ConfigureIpcMediaIngress(
        [weak_probe](const CaptureVideoFrame& frame) {
            if (const auto state = weak_probe.lock()) {
                state->video_frame_index = frame.frame_index_;
            }
        },
        [weak_probe](const CaptureAudioFrame& frame) {
            if (const auto state = weak_probe.lock()) {
                state->audio_frame_index = frame.frame_index_;
            }
        });

    CaptureVideoFrame video;
    video.frame_index_ = 41;
    CaptureAudioFrame audio;
    audio.frame_index_ = 42;
    ws->SubmitIpcVideoFrame(video);
    ws->SubmitIpcAudioFrame(audio);
    EXPECT_EQ(probe->video_frame_index, 41U);
    EXPECT_EQ(probe->audio_frame_index, 42U);

    probe.reset();
    ws->SubmitIpcVideoFrame(video);
    ws->SubmitIpcAudioFrame(audio);
}

}  // namespace
}  // namespace px
