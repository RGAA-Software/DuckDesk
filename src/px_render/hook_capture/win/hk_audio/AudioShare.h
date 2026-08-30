#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "SimpleAudioFormatConverter.h"

namespace px {

class Data;
class Thread;

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

    // Idempotent. Safe when invoked by the IPC sender callback itself.
    void Stop();

    static std::wstring DefaultWavPath(uint32_t pid);

private:
    struct Packet {
        std::shared_ptr<Data> data;
        SimpleAudioFormat format = SimpleAudioFormat::kPCM_S16;
        int samples = 48000;
        int channels = 2;
    };

    class State;

    static void WorkerMain(const std::shared_ptr<State>& state);
    static void SendIpc(const std::shared_ptr<State>& state, const Packet& pkt);

    std::shared_ptr<State> state_;
    std::shared_ptr<Thread> worker_;
};

}  // namespace px
