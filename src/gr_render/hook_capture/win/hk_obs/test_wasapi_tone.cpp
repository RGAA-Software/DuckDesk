// Minimal WASAPI shared-mode sine player for hook-path verification.
// Plays until killed. Usage: test_wasapi_tone.exe

#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <cmath>
#include <cstdint>
#include <iostream>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

int wmain() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        return 1;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    WAVEFORMATEX* mix = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        return 2;
    }
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        return 3;
    }
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&client));
    if (FAILED(hr)) {
        return 4;
    }
    hr = client->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) {
        return 5;
    }
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, mix, nullptr);
    if (FAILED(hr)) {
        return 6;
    }
    UINT32 buffer_frames = 0;
    client->GetBufferSize(&buffer_frames);
    hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render));
    if (FAILED(hr)) {
        return 7;
    }

    std::wcout << L"WASAPI tone " << mix->nSamplesPerSec << L"Hz " << mix->nChannels << L"ch "
               << mix->wBitsPerSample << L"bit — playing until killed\n";
    client->Start();

    double phase = 0.0;
    const double freq = 440.0;
    const double two_pi = 6.283185307179586;
    const bool is_float = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                          (mix->wBitsPerSample == 32 && mix->wFormatTag != WAVE_FORMAT_PCM);

    while (true) {
        UINT32 padding = 0;
        client->GetCurrentPadding(&padding);
        const UINT32 available = buffer_frames - padding;
        if (available < 64) {
            Sleep(5);
            continue;
        }
        BYTE* data = nullptr;
        if (FAILED(render->GetBuffer(available, &data)) || !data) {
            Sleep(5);
            continue;
        }
        const int ch = mix->nChannels > 0 ? mix->nChannels : 2;
        const double step = two_pi * freq / mix->nSamplesPerSec;
        if (is_float) {
            auto* f = reinterpret_cast<float*>(data);
            for (UINT32 i = 0; i < available; i++) {
                const float s = static_cast<float>(0.2 * std::sin(phase));
                phase += step;
                if (phase > two_pi) {
                    phase -= two_pi;
                }
                for (int c = 0; c < ch; c++) {
                    f[i * ch + c] = s;
                }
            }
        } else {
            auto* s16 = reinterpret_cast<int16_t*>(data);
            for (UINT32 i = 0; i < available; i++) {
                const auto s = static_cast<int16_t>(0.2 * 32767.0 * std::sin(phase));
                phase += step;
                if (phase > two_pi) {
                    phase -= two_pi;
                }
                for (int c = 0; c < ch; c++) {
                    s16[i * ch + c] = s;
                }
            }
        }
        render->ReleaseBuffer(available, 0);
    }
}
