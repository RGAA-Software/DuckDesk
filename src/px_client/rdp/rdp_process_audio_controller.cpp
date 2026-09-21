#include "rdp_process_audio_controller.h"

#include <Windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <wrl/client.h>

namespace px::rdp {

ProcessAudioController::ProcessAudioController() {
    const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ownsComInitialization_ = result == S_OK || result == S_FALSE;
    comAvailable_ = ownsComInitialization_ || result == RPC_E_CHANGED_MODE;
}

ProcessAudioController::~ProcessAudioController() {
    if (ownsComInitialization_) {
        CoUninitialize();
    }
}

bool ProcessAudioController::ApplyMuted(const bool muted) const {
    if (!comAvailable_) return false;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator{};
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(deviceEnumerator.GetAddressOf())))) {
        return false;
    }

    Microsoft::WRL::ComPtr<IMMDevice> device{};
    if (FAILED(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, device.GetAddressOf()))) return false;

    Microsoft::WRL::ComPtr<IAudioSessionManager2> sessionManager{};
    if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr,
                                reinterpret_cast<void**>(sessionManager.GetAddressOf())))) {
        return false;
    }

    Microsoft::WRL::ComPtr<IAudioSessionEnumerator> sessions{};
    if (FAILED(sessionManager->GetSessionEnumerator(sessions.GetAddressOf()))) return false;

    int sessionCount{};
    if (FAILED(sessions->GetCount(&sessionCount))) return false;

    const DWORD processId = GetCurrentProcessId();
    bool matched{};
    for (int index{}; index < sessionCount; ++index) {
        Microsoft::WRL::ComPtr<IAudioSessionControl> session{};
        if (FAILED(sessions->GetSession(index, session.GetAddressOf()))) continue;

        Microsoft::WRL::ComPtr<IAudioSessionControl2> processSession{};
        if (FAILED(session.As(&processSession))) continue;

        DWORD sessionProcessId{};
        if (FAILED(processSession->GetProcessId(&sessionProcessId)) || sessionProcessId != processId) continue;

        Microsoft::WRL::ComPtr<ISimpleAudioVolume> volume{};
        if (FAILED(session.As(&volume)) || FAILED(volume->SetMute(muted ? TRUE : FALSE, nullptr))) return false;
        matched = true;
    }
    return matched;
}

}  // namespace px::rdp
