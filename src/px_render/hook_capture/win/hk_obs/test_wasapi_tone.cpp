// Minimal WASAPI shared-mode sine player for hook-path verification.
// Plays until killed. Usage: test_wasapi_tone.exe

#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

namespace {

class ComApartment final {
public:
    ComApartment() : result_{CoInitializeEx(nullptr, COINIT_MULTITHREADED)}, mustUninitialize_{result_ == S_OK || result_ == S_FALSE} {}
    ~ComApartment() {
        if (mustUninitialize_) {
            CoUninitialize();
        }
    }

    [[nodiscard]] HRESULT Result() const noexcept { return result_; }

private:
    HRESULT result_{};
    bool mustUninitialize_{};
};

struct CoTaskMemoryDeleter final {
    void operator()(WAVEFORMATEX* const format) const noexcept {  // NOLINT(pixels-raw-pointer-boundary): COM task-memory deleter ABI.
        CoTaskMemFree(format);
    }
};

}  // namespace

int wmain() {
    const ComApartment apartment{};
    if (FAILED(apartment.Result()) && apartment.Result() != RPC_E_CHANGED_MODE) {
        return 1;
    }

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator{};
    Microsoft::WRL::ComPtr<IMMDevice> playbackDevice{};
    Microsoft::WRL::ComPtr<IAudioClient> audioClient{};
    Microsoft::WRL::ComPtr<IAudioRenderClient> audioRenderer{};

    auto result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(deviceEnumerator.ReleaseAndGetAddressOf()));
    if (FAILED(result)) {
        return 2;
    }
    result = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, playbackDevice.ReleaseAndGetAddressOf());
    if (FAILED(result)) {
        return 3;
    }
    result = playbackDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(audioClient.ReleaseAndGetAddressOf()));
    if (FAILED(result)) {
        return 4;
    }
    WAVEFORMATEX* mixFormatBoundary{};  // NOLINT(pixels-raw-pointer-boundary): transient COM out parameter, immediately smart-owned.
    result = audioClient->GetMixFormat(&mixFormatBoundary);
    std::unique_ptr<WAVEFORMATEX, CoTaskMemoryDeleter> mixFormat{mixFormatBoundary};
    if (FAILED(result) || !mixFormat) {
        return 5;
    }
    result = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10'000'000, 0, mixFormat.get(), nullptr);
    if (FAILED(result)) {
        return 6;
    }
    UINT32 bufferFrameCount{};
    if (FAILED(audioClient->GetBufferSize(&bufferFrameCount))) {
        return 7;
    }
    result = audioClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(audioRenderer.ReleaseAndGetAddressOf()));
    if (FAILED(result)) {
        return 7;
    }

    std::wcout << L"WASAPI tone " << mixFormat->nSamplesPerSec << L"Hz " << mixFormat->nChannels << L"ch " << mixFormat->wBitsPerSample
               << L"bit - playing until killed\n";
    if (FAILED(audioClient->Start())) {
        return 8;
    }

    double phase{};
    constexpr double frequency{440.0};
    constexpr double twoPi{6.283185307179586};
    const bool floatingPointSamples =
        mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || (mixFormat->wBitsPerSample == 32 && mixFormat->wFormatTag != WAVE_FORMAT_PCM);

    while (true) {
        UINT32 paddingFrameCount{};
        if (FAILED(audioClient->GetCurrentPadding(&paddingFrameCount))) {
            return 9;
        }
        const UINT32 availableFrameCount{bufferFrameCount - paddingFrameCount};
        if (availableFrameCount < 64U) {
            Sleep(5);
            continue;
        }
        BYTE* audioBufferBoundary{};  // NOLINT(pixels-raw-pointer-boundary): transient WASAPI buffer valid until ReleaseBuffer.
        if (FAILED(audioRenderer->GetBuffer(availableFrameCount, &audioBufferBoundary)) || audioBufferBoundary == nullptr) {
            Sleep(5);
            continue;
        }
        const int channelCount{mixFormat->nChannels > 0 ? mixFormat->nChannels : 2};
        const double phaseStep{twoPi * frequency / mixFormat->nSamplesPerSec};
        if (floatingPointSamples) {
            const std::span<float> outputSamples{reinterpret_cast<float*>(audioBufferBoundary),
                                                 static_cast<std::size_t>(availableFrameCount) * channelCount};
            for (UINT32 frameIndex{}; frameIndex < availableFrameCount; ++frameIndex) {
                const float sample = static_cast<float>(0.2 * std::sin(phase));
                phase += phaseStep;
                if (phase > twoPi) {
                    phase -= twoPi;
                }
                for (int channelIndex{}; channelIndex < channelCount; ++channelIndex) {
                    outputSamples[static_cast<std::size_t>(frameIndex) * channelCount + channelIndex] = sample;
                }
            }
        } else {
            const std::span<std::int16_t> outputSamples{reinterpret_cast<std::int16_t*>(audioBufferBoundary),
                                                        static_cast<std::size_t>(availableFrameCount) * channelCount};
            for (UINT32 frameIndex{}; frameIndex < availableFrameCount; ++frameIndex) {
                const auto sample = static_cast<std::int16_t>(0.2 * 32'767.0 * std::sin(phase));
                phase += phaseStep;
                if (phase > twoPi) {
                    phase -= twoPi;
                }
                for (int channelIndex{}; channelIndex < channelCount; ++channelIndex) {
                    outputSamples[static_cast<std::size_t>(frameIndex) * channelCount + channelIndex] = sample;
                }
            }
        }
        if (FAILED(audioRenderer->ReleaseBuffer(availableFrameCount, 0))) {
            return 10;
        }
    }
}
