#include "AudioShare.h"

#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <filesystem>

#include "px_capture_new/capture_message_maker.h"
#include "px_common_new/data.h"
#include "px_common_new/folder_util.h"
#include "px_common_new/log.h"
#include "px_common_new/string_util.h"

namespace tc {
namespace {

constexpr size_t kMaxQueueBytes = 16 * 1024 * 1024;  // byte-based, not packet count

#if 0  // WAV dump disabled — keep for local debug only
#pragma pack(push, 1)
struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunk_size = 36;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    uint16_t num_channels = 2;
    uint32_t sample_rate = 48000;
    uint32_t byte_rate = 0;
    uint16_t block_align = 0;
    uint16_t bits_per_sample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size = 0;
};
#pragma pack(pop)
#endif

}  // namespace

std::wstring AudioShare::DefaultWavPath(uint32_t pid) {
    auto dir = FolderUtil::GetProgramDataPath();
    return (std::filesystem::path(dir) / std::format(L"hook_audio_{}.wav", pid)).wstring();
}

AudioShare::AudioShare() {
    write_wav_.store(false, std::memory_order_relaxed);
    worker_ = std::thread([this] { WorkerMain(); });
    LOGI("AudioShare: worker started (wav_dump=off, ipc=on)");
}

AudioShare::~AudioShare() {
    stop_.store(true, std::memory_order_release);
    q_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
#if 0  // WAV dump disabled
    std::lock_guard lock(mu_);
    CloseWavUnlocked();
#endif
    LOGI("AudioShare: stopped ipc_frames={}", ipc_frames_.load());
}

void AudioShare::SetIpcSender(IpcSender sender) {
    std::lock_guard lock(ipc_mu_);
    ipc_sender_ = std::move(sender);
    LOGI("AudioShare: IPC sender {}", ipc_sender_ ? "installed" : "cleared");
}

void AudioShare::SetWriteWav(bool enable) {
    write_wav_.store(enable, std::memory_order_relaxed);
    LOGI("AudioShare: SetWriteWav={}", enable);
}

void AudioShare::SetAudioFormat(SimpleAudioFormat format, int samples, int channels, int bits) {
    (void)bits;
    std::lock_guard lock(fmt_mu_);
    src_format_ = format;
    src_samples_ = samples > 0 ? samples : 48000;
    src_channels_ = channels > 0 ? channels : 2;
}

void AudioShare::PostAudioData(std::shared_ptr<Data> data) {
    if (!data || data->Size() == 0) {
        LOGE("AudioShare::PostAudioData: empty data");
        return;
    }
    if (stop_.load(std::memory_order_acquire)) {
        LOGE("AudioShare::PostAudioData: already stopped");
        return;
    }

    Packet pkt;
    pkt.data = data;
    {
        std::lock_guard lock(fmt_mu_);
        pkt.format = src_format_;
        pkt.samples = src_samples_;
        pkt.channels = src_channels_;
    }

    const size_t pkt_bytes = static_cast<size_t>(pkt.data->Size());
    uint64_t dropped = 0;
    {
        std::lock_guard lock(q_mu_);
        // Byte-based cap: drop oldest until the new packet fits.
        while (q_bytes_ + pkt_bytes > kMaxQueueBytes && !q_.empty()) {
            q_bytes_ -= static_cast<size_t>(q_.front().data ? q_.front().data->Size() : 0);
            q_.pop();
            dropped = dropped_.fetch_add(1, std::memory_order_relaxed) + 1;
        }
        q_bytes_ += pkt_bytes;
        q_.push(std::move(pkt));
    }
    if (dropped && (dropped == 1 || (dropped % 100) == 0)) {
        LOGW("AudioShare: queue over {} bytes, dropped oldest n={}", kMaxQueueBytes, dropped);
    }
    q_cv_.notify_one();
}

void AudioShare::SendIpcUnlocked(const Packet& pkt) {
    IpcSender sender;
    {
        std::lock_guard lock(ipc_mu_);
        sender = ipc_sender_;
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
        pkt.data->CStr(), pkt.data->Size(), static_cast<uint32_t>(pkt.samples),
        static_cast<uint32_t>(pkt.channels), 16, ipc_frames_.load(std::memory_order_relaxed));
    if (msg.empty()) {
        LOGE("AudioShare IPC: MakeIpcAudioFrameString failed bytes={}", pkt.data->Size());
        return;
    }
    sender(std::move(msg));
    const auto n = ++ipc_frames_;
    if (n == 1 || (n % 200) == 0) {
        LOGI("AudioShare IPC: frames={} bytes={} {}Hz {}ch", n, pkt.data->Size(), pkt.samples,
             pkt.channels);
    }
}

void AudioShare::WorkerMain() {
    LOGI("AudioShare: WorkerMain enter");
    while (true) {
        Packet pkt;
        {
            std::unique_lock lock(q_mu_);
            q_cv_.wait(lock, [&] { return stop_.load(std::memory_order_acquire) || !q_.empty(); });
            if (q_.empty()) {
                if (stop_.load(std::memory_order_acquire)) {
                    break;
                }
                continue;
            }
            pkt = std::move(q_.front());
            q_bytes_ -= static_cast<size_t>(pkt.data ? pkt.data->Size() : 0);
            q_.pop();
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

        SendIpcUnlocked(pkt);

#if 0  // WAV dump disabled
        if (!write_wav_.load(std::memory_order_relaxed) || !pcm) {
            continue;
        }
        std::lock_guard lock(mu_);
        ApplyFormatUnlocked(pkt.format, pkt.samples, pkt.channels, 16);
        EnsureWavUnlocked();
        if (!file_ || !pcm) {
            LOGE("AudioShare: wav file not open");
            continue;
        }
        fwrite(pcm->DataAddr(), 1, pcm->Size(), file_);
        data_bytes_ += static_cast<uint32_t>(pcm->Size());
        const auto n = ++posted_frames_;
        const bool flush_now = (n == 1) || (data_bytes_ - last_flushed_bytes_ >= 48000);
        if (flush_now) {
            FlushHeaderUnlocked();
            last_flushed_bytes_ = data_bytes_;
        }
        if (n == 1 || (n % 200) == 0) {
            LOGI("AudioShare wav: frames={}, bytes={}", n, data_bytes_);
        }
#endif
    }
    LOGI("AudioShare: WorkerMain exit");
#if 0  // WAV dump disabled
    std::lock_guard lock(mu_);
    FlushHeaderUnlocked();
#endif
}

void AudioShare::ApplyFormatUnlocked(SimpleAudioFormat format, int samples, int channels, int bits) {
#if 0  // WAV dump disabled
    const int new_samples = samples > 0 ? samples : 48000;
    const int new_channels = channels > 0 ? channels : 2;
    const int new_bits = bits > 0 ? bits : 16;
    if (format_ == format && samples_ == new_samples && channels_ == new_channels &&
        bits_ == new_bits) {
        return;
    }
    format_ = format;
    samples_ = new_samples;
    channels_ = new_channels;
    bits_ = new_bits;
    CloseWavUnlocked();
#else
    (void)format;
    (void)samples;
    (void)channels;
    (void)bits;
#endif
}

void AudioShare::FlushHeaderUnlocked() {
#if 0  // WAV dump disabled
    if (!file_) {
        return;
    }
    const long pos = ftell(file_);
    WavHeader hdr{};
    hdr.num_channels = static_cast<uint16_t>(channels_);
    hdr.sample_rate = static_cast<uint32_t>(samples_);
    hdr.bits_per_sample = static_cast<uint16_t>(bits_);
    hdr.block_align = static_cast<uint16_t>(channels_ * bits_ / 8);
    hdr.byte_rate = hdr.sample_rate * hdr.block_align;
    hdr.data_size = data_bytes_;
    hdr.chunk_size = 36 + data_bytes_;
    fseek(file_, 0, SEEK_SET);
    fwrite(&hdr, sizeof(hdr), 1, file_);
    fflush(file_);
    if (pos >= 0) {
        fseek(file_, pos, SEEK_SET);
    }
#endif
}

void AudioShare::EnsureWavUnlocked() {
#if 0  // WAV dump disabled
    if (file_) {
        return;
    }
    wav_path_ = DefaultWavPath(GetCurrentProcessId());
    file_ = _wfopen(wav_path_.c_str(), L"wb");
    if (!file_) {
        LOGE("AudioShare: open wav failed: {}", StringUtil::ToUTF8(wav_path_));
        return;
    }
    data_bytes_ = 0;
    last_flushed_bytes_ = 0;
    FlushHeaderUnlocked();
    LOGI("AudioShare: writing wav {}", StringUtil::ToUTF8(wav_path_));
#endif
}

void AudioShare::CloseWavUnlocked() {
#if 0  // WAV dump disabled
    if (!file_) {
        return;
    }
    FlushHeaderUnlocked();
    fclose(file_);
    file_ = nullptr;
    LOGI("AudioShare: closed wav {}, pcm_bytes={}", StringUtil::ToUTF8(wav_path_), data_bytes_);
#endif
}

}  // namespace tc
