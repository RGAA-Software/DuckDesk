#pragma once

#include "media_transport/audio_stream.h"

#include "media_transport/nanors_codec.h"
#include <memory>

namespace px::media::test {
class UpstreamAudioOracle final {
  public:
    UpstreamAudioOracle();
    ~UpstreamAudioOracle();
    [[nodiscard]] std::vector<AudioDelivery> Feed(const Packet& packet, std::uint64_t now_us);

  private:
    struct State;
    std::unique_ptr<State> state_{};
};
struct OracleLoss final {
    std::uint32_t frame{};
    bool speculative{};
};
struct OracleResults final {
    std::vector<Packet> data_packets{};
    std::vector<OracleLoss> losses{};
    std::size_t fec_reports{};
};

[[nodiscard]] bool DecodeWithUpstreamAudioMatrix(std::vector<Packet>& shards, std::span<const std::uint8_t> missing);

// Isolated reference executable only: upstream globals permit one active oracle per thread/process.
class UpstreamVideoOracle final {
  public:
    explicit UpstreamVideoOracle(std::uint16_t datagram_size);
    ~UpstreamVideoOracle();
    UpstreamVideoOracle(const UpstreamVideoOracle&) = delete;
    UpstreamVideoOracle& operator=(const UpstreamVideoOracle&) = delete;
    void Feed(const Packet& packet);
    [[nodiscard]] OracleResults Results() const;

  private:
    struct State;
    std::unique_ptr<State> state_{};
};
} // namespace px::media::test
