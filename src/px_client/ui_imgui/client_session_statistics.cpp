#include <algorithm>
#include <map>
#include <span>
#include <vector>

#include "client_session.h"
#include "px_client_sdk/sdk_statistics.h"

namespace px::client::imgui {
namespace {

std::optional<float> LastSample(const std::vector<float>& samples) { return samples.empty() ? std::nullopt : std::optional{samples.back()}; }

std::vector<float> MonitorSamples(const std::map<std::string, std::vector<float>>& samples, const std::string& monitorName) {
    if (const auto monitor = samples.find(monitorName); monitor != samples.end()) return monitor->second;
    return samples.size() == 1 ? samples.begin()->second : std::vector<float>{};
}

std::optional<float> RecentAverage(const std::vector<float>& samples) {
    if (samples.empty()) return std::nullopt;
    const std::span<const float> recent{std::span<const float>{samples}.last(std::min(std::size_t{30}, samples.size()))};
    float sum{};
    for (const float measurement : recent) sum += measurement;
    return sum / static_cast<float>(recent.size());
}

}  // namespace

ClientStatisticsSnapshot ClientSession::StatisticsSnapshot() const {
    const auto session = Snapshot();
    ClientStatisticsSnapshot result{.transport = config_.iroh ? (config_.rdp ? ClientStatisticsTransport::RdpIroh : ClientStatisticsTransport::Iroh)
                                                 : config_.rdp          ? ClientStatisticsTransport::Rdp
                                                 : config_.forceRelay ? ClientStatisticsTransport::RelayWebSocket
                                                 : config_.forceTcp   ? ClientStatisticsTransport::DirectWebSocket
                                                                      : ClientStatisticsTransport::DirectUdp,
                                    .frameWidth = session.frame ? session.frame->width : 0,
                                    .frameHeight = session.frame ? session.frame->height : 0,
                                    .decodedFps = session.framesPerSecond,
                                    .latencyMilliseconds = session.latencyMilliseconds,
                                    .bitrateKbps = session.bitrateKbps,
                                    .decodedAudioFrames = session.decodedAudioFrames,
                                    .decodedAudioBytes = session.decodedAudioBytes,
                                    .decoder = session.decoder};
    std::shared_ptr<px::SdkStatistics> statistics{};
    {
        const std::scoped_lock lock{mutex_};
        statistics = statistics_;
    }
    if (!statistics) return result;
    if (!config_.rdp && !config_.forceRelay && statistics->media_transport_.load() == px::SdkMediaTransport::kWebSocket) {
        result.transport = ClientStatisticsTransport::DirectWebSocket;
    }
    result.receivedFps = LastSample(MonitorSamples(statistics->GetVideoRecvFps(), session.monitorName));
    result.receiveMegabytesPerSecond = LastSample(statistics->GetRecvDataSpeeds());
    result.sendMegabytesPerSecond = LastSample(statistics->GetSendDataSpeeds());
    result.decodeMilliseconds = RecentAverage(MonitorSamples(statistics->GetDecodeDurations(), session.monitorName));
    result.receiveGapMilliseconds = RecentAverage(MonitorSamples(statistics->GetVideoRecvGaps(), session.monitorName));
    result.receivedBytes = static_cast<std::uint64_t>(std::max(std::int64_t{}, statistics->recv_data_size_.load()));
    result.sentBytes = static_cast<std::uint64_t>(std::max(std::int64_t{}, statistics->send_data_size_.load()));
    result.videoFormat = statistics->video_format_.Clone();
    result.videoColor = statistics->video_color_.Clone();
    result.videoCapture = statistics->video_capture_type_.Clone();
    result.audioCapture = statistics->audio_capture_type_.Clone();
    result.audioCodec = statistics->audio_encode_type_.Clone();
    const auto monitors = statistics->GetRenderMonitorsStat();
    auto remoteMonitor = monitors.find(session.monitorName);
    if (remoteMonitor == monitors.end() && monitors.size() == 1) remoteMonitor = monitors.begin();
    if (remoteMonitor != monitors.end()) {
        result.captureFps = remoteMonitor->second.capture_fps();
        result.encodeFps = remoteMonitor->second.encode_fps();
        result.encoder = remoteMonitor->second.encoder_name();
    }
    return result;
}

}  // namespace px::client::imgui
