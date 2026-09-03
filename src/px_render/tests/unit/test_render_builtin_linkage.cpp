#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "amf_encoder/amf_encoder_plugin.h"
#include "dda_capture/dda_capture_plugin.h"
#include "ffmpeg_encoder/ffmpeg_encoder_plugin.h"
#include "gdi_capture/gdi_capture_plugin.h"
#include "net_relay/relay_plugin.h"
#include "net_udp/udp_plugin.h"
#include "net_ws/ws_plugin.h"
#include "nvenc_encoder/nvenc_encoder_plugin.h"

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

TEST(RenderBuiltinLinkageTest, WsTransportPeerInjectionUsesWeakLifetime) {
    const auto ws = std::make_shared<WsPlugin>();
    auto udp = std::make_shared<UdpPlugin>();
    const auto relay = std::make_shared<RelayPlugin>();

    for (int round = 0; round < 100; ++round) {
        ws->ConfigureNetworkPeers({ws, udp, relay});
        EXPECT_EQ(ws->GetNetworkPeers().size(), 3U);
    }
    EXPECT_EQ(ws->GetUdpTransport(), udp);

    udp.reset();
    const auto remaining = ws->GetNetworkPeers();
    EXPECT_EQ(remaining.size(), 2U);
    EXPECT_FALSE(ws->GetUdpTransport());
}

}  // namespace
}  // namespace px
