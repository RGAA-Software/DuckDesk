#include "AudioShare.h"

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <queue>

#include "px_capture_new/capture_message_maker.h"
#include "px_common_new/data.h"
#include "px_common_new/folder_util.h"
#include "px_common_new/log.h"
#include "px_common_new/thread.h"

namespace px {
namespace {

constexpr size_t kMaxQueueBytes = 16 * 1024 * 1024;  // byte-based, not packet count

}  // namespace

class AudioShare::State {
public:
    std::mutex q_mu;
    std::condition_variable q_cv;
    std::queue<Packet> q;
    size_t q_bytes = 0;
    std::atomic<uint64_t> dropped{0};
    std::atomic<bool> stop{false};

    std::mutex fmt_mu;
    SimpleAudioFormat src_format = SimpleAudioFormat::kPCM_S16;
    int src_samples = 48000;
    int src_channels = 2;

    std::mutex ipc_mu;
    IpcSender ipc_sender;
    std::atomic<uint64_t> ipc_frames{0};
    std::atomic<bool> write_wav{false};
};

std::wstring AudioShare::DefaultWavPath(uint32_t pid) {
    auto dir = FolderUtil::GetProgramDataPath();
    return (std::filesystem::path(dir) / std::format(L"hook_audio_{}.wav", pid)).wstring();
}

AudioShare::AudioShare() : state_(std::make_shared<State>()) {
    const auto state = state_;
    worker_ = Thread::MakeOnceTask([state]() { WorkerMain(state); }, "hook_audio_share", false);
    LOGI("AudioShare: worker started (wav_dump=off, ipc=on)");
}

AudioShare::~AudioShare() {
    const auto state = state_;
    Stop();
    LOGI("AudioShare: stopped ipc_frames={}",
         state ? state->ipc_frames.load(std::memory_order_relaxed) : 0);
}

void AudioShare::Stop() {
    const auto state = state_;
    if (!state) {
        return;
    }
    state->stop.store(true, std::memory_order_release);
    state->q_cv.notify_all();
    const auto worker = std::move(worker_);
    if (worker) {
        worker->Exit();
    }
}

void AudioShare::SetIpcSender(IpcSender sender) {
    const auto state = state_;
    if (!state) {
        return;
    }
    std::lock_guard lock(state->ipc_mu);
    state->ipc_sender = std::move(sender);
    LOGI("AudioShare: IPC sender {}", state->ipc_sender ? "installed" : "cleared");
}

void AudioShare::SetWriteWav(bool enable) {
    const auto state = state_;
    if (state) {
        state->write_wav.store(enable, std::memory_order_relaxed);
    }
    LOGI("AudioShare: SetWriteWav={}", enable);
}

void AudioShare::SetAudioFormat(SimpleAudioFormat format, int samples, int channels, int bits) {
    (void)bits;
    const auto state = state_;
    if (!state) {
        return;
    }
    std::lock_guard lock(state->fmt_mu);
    state->src_format = format;
    state->src_samples = samples > 0 ? samples : 48000;
    state->src_channels = channels > 0 ? channels : 2;
}

void AudioShare::PostAudioData(std::shared_ptr<Data> data) {
    if (!data || data->Size() == 0) {
        LOGE("AudioShare::PostAudioData: empty data");
        return;
    }
    const auto state = state_;
    if (!state || state->stop.load(std::memory_order_acquire)) {
        LOGE("AudioShare::PostAudioData: already stopped");
        return;
    }

    Packet pkt;
    pkt.data = data;
    {
        std::lock_guard lock(state->fmt_mu);
        pkt.format = state->src_format;
        pkt.samples = state->src_samples;
        pkt.channels = state->src_channels;
    }

    const size_t pkt_bytes = static_cast<size_t>(pkt.data->Size());
    uint64_t dropped = 0;
    {
        std::lock_guard lock(state->q_mu);
        if (state->stop.load(std::memory_order_acquire)) {
            return;
        }
        // Byte-based cap: drop oldest until the new packet fits.
        while (state->q_bytes + pkt_bytes > kMaxQueueBytes && !state->q.empty()) {
            state->q_bytes -=
                static_cast<size_t>(state->q.front().data ? state->q.front().data->Size() : 0);
            state->q.pop();
            dropped = state->dropped.fetch_add(1, std::memory_order_relaxed) + 1;
        }
        state->q_bytes += pkt_bytes;
        state->q.push(std::move(pkt));
    }
    if (dropped && (dropped == 1 || (dropped % 100) == 0)) {
        LOGW("AudioShare: queue over {} bytes, dropped oldest n={}", kMaxQueueBytes, dropped);
    }
    state->q_cv.notify_one();
}

void AudioShare::SendIpc(const std::shared_ptr<State>& state, const Packet& pkt) {
    IpcSender sender;
    {
        std::lock_guard lock(state->ipc_mu);
        sender = state->ipc_sender;
    }
    if (!sender) {
        static std::atomic<uint64_t> s_no_sender{0};
        if (++s_no_sender == 1 || (s_no_sender.load() % 200) == 0) {
            LOGE("AudioShare IPC: no sender installed n={}", s_no_sender.load());
        }
        return;
    }
    if (!pkt.data) {
        LOGE("AudioShare IPC: null pcm packet");
        return;
    }
    auto msg = CaptureMessageMaker::MakeIpcAudioFrameString(
        pkt.data->Bytes().data(), pkt.data->Size(), static_cast<uint32_t>(pkt.samples),
        static_cast<uint32_t>(pkt.channels), 16,
        state->ipc_frames.load(std::memory_order_relaxed));
    if (msg.empty()) {
        LOGE("AudioShare IPC: MakeIpcAudioFrameString failed bytes={}", pkt.data->Size());
        return;
    }
    sender(std::move(msg));
    const auto n = ++state->ipc_frames;
    if (n == 1 || (n % 200) == 0) {
        LOGI("AudioShare IPC: frames={} bytes={} {}Hz {}ch", n, pkt.data->Size(), pkt.samples,
             pkt.channels);
    }
}

void AudioShare::WorkerMain(const std::shared_ptr<State>& state) {
    LOGI("AudioShare: WorkerMain enter");
    while (true) {
        Packet pkt;
        {
            std::unique_lock lock(state->q_mu);
            state->q_cv.wait(lock, [state] {
                return state->stop.load(std::memory_order_acquire) || !state->q.empty();
            });
            if (state->q.empty()) {
                if (state->stop.load(std::memory_order_acquire)) {
                    break;
                }
                continue;
            }
            pkt = std::move(state->q.front());
            state->q_bytes -= static_cast<size_t>(pkt.data ? pkt.data->Size() : 0);
            state->q.pop();
        }

        auto pcm = pkt.data;
        if (pkt.format == SimpleAudioFormat::kPCM_F32) {
            pcm = SimpleAudioFormatConverter::CvtF32ToS16(pkt.data);
            if (!pcm) {
                LOGE("AudioShare: F32→S16 convert failed");
                continue;
            }
            pkt.data = pcm;
            pkt.format = SimpleAudioFormat::kPCM_S16;
        }

        SendIpc(state, pkt);
    }
    LOGI("AudioShare: WorkerMain exit");
}

}  // namespace px
