#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "SimpleAudioFormatConverter.h"

namespace px {

class Data;

// Collects hooked PCM and forwards to host via /ipc.
// Local WAV dump is disabled by default (SetWriteWav).
class AudioShare {
public:
    using IpcSender = std::function<void(std::string&&)>;

    AudioShare();
    ~AudioShare();

    void SetAudioFormat(SimpleAudioFormat format, int samples, int channels, int bits);
    void PostAudioData(std::shared_ptr<Data> data);

    // Host-bound sender (dll -> px_render /ipc). Set after WsIpcClient is up.
    void SetIpcSender(IpcSender sender);
    void SetWriteWav(bool enable);

    static std::wstring DefaultWavPath(uint32_t pid);

private:
    struct Packet {
        std::shared_ptr<Data> data;
        SimpleAudioFormat format = SimpleAudioFormat::kPCM_S16;
        int samples = 48000;
        int channels = 2;
    };

    void WorkerMain();
    void EnsureWavUnlocked();
    void CloseWavUnlocked();
    void FlushHeaderUnlocked();
    void ApplyFormatUnlocked(SimpleAudioFormat format, int samples, int channels, int bits);
    void SendIpcUnlocked(const Packet& pkt);

    SimpleAudioFormat format_ = SimpleAudioFormat::kPCM_S16;
    int samples_ = 48000;
    int channels_ = 2;
    int bits_ = 16;

    std::mutex mu_;
    FILE* file_ = nullptr;
    uint32_t data_bytes_ = 0;
    uint32_t last_flushed_bytes_ = 0;
    std::wstring wav_path_;
    std::atomic<uint64_t> posted_frames_{0};
    std::atomic<uint64_t> ipc_frames_{0};
    std::atomic<bool> write_wav_{false};

    std::mutex q_mu_;
    std::condition_variable q_cv_;
    std::queue<Packet> q_;
    size_t q_bytes_ = 0;  // guarded by q_mu_
    std::atomic<uint64_t> dropped_{0};
    std::atomic<bool> stop_{false};
    std::thread worker_;

    std::mutex fmt_mu_;
    SimpleAudioFormat src_format_ = SimpleAudioFormat::kPCM_S16;
    int src_samples_ = 48000;
    int src_channels_ = 2;

    std::mutex ipc_mu_;
    IpcSender ipc_sender_;
};

}  // namespace px
