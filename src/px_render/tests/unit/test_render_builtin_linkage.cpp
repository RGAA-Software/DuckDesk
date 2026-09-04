#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "px_common_new/data.h"
#include "amf_encoder/amf_encoder_plugin.h"
#include "dda_capture/dda_capture_plugin.h"
#include "ffmpeg_encoder/ffmpeg_encoder_plugin.h"
#include "gdi_capture/gdi_capture_plugin.h"
#include "net_relay/relay_plugin.h"
#include "net_udp/udp_plugin.h"
#include "net_ws/ws_plugin.h"
#include "nvenc_encoder/nvenc_encoder_plugin.h"
#include "px_render/plugin_interface/px_plugin_events.h"

namespace px {
namespace {

TEST(RenderBuiltinLinkageTest, ProductionModulesHaveStaticConstructorsAndIds) {
    const std::vector<std::shared_ptr<PxPluginInterface>> modules{
        std::make_shared<DDACapturePlugin>(),
        std::make_shared<GdiCapturePlugin>(),
        std::make_shared<FFmpegEncoderPlugin>(),
        std::make_shared<AmfEncoderPlugin>(),
        std::make_shared<NvencEncoderPlugin>(),
        std::make_shared<WsPlugin>(),
        std::make_shared<UdpPlugin>(),
        std::make_shared<RelayPlugin>(),
    };
    for (const auto& module : modules) {
        ASSERT_TRUE(module);
        EXPECT_FALSE(module->GetPluginId().empty());
        EXPECT_FALSE(module->GetPluginName().empty());
    }
}

TEST(RenderBuiltinLinkageTest, VideoEncoderMetadataContractIsStronglyTyped) {
    using CpuEncode = VideoEncoderError (PxVideoEncoderPlugin::*)(
        const std::shared_ptr<Image>&, std::uint64_t,
        const CaptureVideoFrame&);
    using TextureEncode = VideoEncoderError (PxVideoEncoderPlugin::*)(
        const Microsoft::WRL::ComPtr<ID3D11Texture2D>&, std::uint64_t,
        const CaptureVideoFrame&);

    const auto cpu_encode = static_cast<CpuEncode>(
        &PxVideoEncoderPlugin::Encode);
    const auto texture_encode = static_cast<TextureEncode>(
        &PxVideoEncoderPlugin::Encode);
    EXPECT_NE(cpu_encode, nullptr);
    EXPECT_NE(texture_encode, nullptr);
}

struct NetworkServiceProbe final {
    int network_broadcasts = 0;
    int file_transfer_broadcasts = 0;
    int rtc_allocations = 0;
    int udp_updates = 0;
};

TEST(RenderBuiltinLinkageTest, WsUsesExplicitNetworkCapabilitiesWithWeakLifetime) {
    const auto ws = std::make_shared<WsPlugin>();
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
                     WsPlugin::LocalRtcCompletion completion) {
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

}  // namespace
}  // namespace px
