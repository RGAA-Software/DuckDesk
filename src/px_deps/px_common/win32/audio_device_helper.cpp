//
// Created by RGAA on 6/08/2024.
//

#include "audio_device_helper.h"

#include <Windows.h>
#include <propsys.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <Mmdeviceapi.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

#include "px_common/log.h"
#include "px_common/scope_exit.h"
#include "px_common/string_util.h"

namespace px {
namespace {

using Microsoft::WRL::ComPtr;

struct CoTaskMemoryCloser final {
    void operator()(wchar_t* value) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): CoTaskMemFree ABI.
        CoTaskMemFree(value);
    }
};

using UniqueCoTaskString = std::unique_ptr<wchar_t, CoTaskMemoryCloser>;

class PropVariantValue final {
public:
    PropVariantValue() { PropVariantInit(&value_); }
    ~PropVariantValue() { PropVariantClear(&value_); }

    PropVariantValue(const PropVariantValue&) = delete;
    PropVariantValue& operator=(const PropVariantValue&) = delete;

    [[nodiscard]] PROPVARIANT& Get() noexcept { return value_; }

private:
    PROPVARIANT value_{};
};

}  // namespace

std::vector<AudioDevice> AudioDeviceHelper::DetectAudioDevices() {
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
        LOGE("event=audio_device.enumerate stage=com_init outcome=failed hresult={:#x}", static_cast<std::uint32_t>(com_result));
        return {};
    }
    const auto uninitialize_com = PxScopeExit{[should_uninitialize = com_result == S_OK || com_result == S_FALSE] {
        if (should_uninitialize) {
            CoUninitialize();
        }
    }};

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(enumerator.GetAddressOf()));
    if (FAILED(result)) {
        LOGE("event=audio_device.enumerate stage=create_enumerator outcome=failed hresult={:#x}",
             static_cast<std::uint32_t>(result));
        return {};
    }

    ComPtr<IMMDeviceCollection> devices;
    result = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.GetAddressOf());
    if (FAILED(result)) {
        LOGE("event=audio_device.enumerate stage=list_endpoints outcome=failed hresult={:#x}", static_cast<std::uint32_t>(result));
        return {};
    }

    UINT device_count{};
    result = devices->GetCount(&device_count);
    if (FAILED(result)) {
        LOGE("event=audio_device.enumerate stage=count outcome=failed hresult={:#x}", static_cast<std::uint32_t>(result));
        return {};
    }

    std::vector<AudioDevice> audio_devices;
    audio_devices.reserve(device_count);
    for (UINT index = 0; index < device_count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(index, device.GetAddressOf()))) {
            LOGW("event=audio_device.enumerate stage=get_device outcome=skipped index={}", index);
            continue;
        }

        LPWSTR device_id_raw{};  // NOLINT(gammaray-raw-pointer-boundary): IMMDevice out parameter, immediately RAII-wrapped.
        if (FAILED(device->GetId(&device_id_raw))) {
            continue;
        }
        const UniqueCoTaskString device_id{device_id_raw};

        ComPtr<IPropertyStore> properties;
        if (FAILED(device->OpenPropertyStore(STGM_READ, properties.GetAddressOf()))) {
            continue;
        }

        PropVariantValue friendly_name;
        if (FAILED(properties->GetValue(PKEY_Device_FriendlyName, &friendly_name.Get()))) {
            continue;
        }

        AudioDevice audio_device{};
        audio_device.id_ = StringUtil::ToUTF8(device_id.get());
        if (friendly_name.Get().vt == VT_LPWSTR && friendly_name.Get().pwszVal != nullptr) {
            audio_device.name_ = StringUtil::ToUTF8(friendly_name.Get().pwszVal);
        }
        audio_devices.push_back(std::move(audio_device));
    }

    ComPtr<IMMDevice> default_device;
    result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, default_device.GetAddressOf());
    if (SUCCEEDED(result)) {
        LPWSTR default_id_raw{};  // NOLINT(gammaray-raw-pointer-boundary): IMMDevice out parameter, immediately RAII-wrapped.
        if (SUCCEEDED(default_device->GetId(&default_id_raw))) {
            const UniqueCoTaskString default_id{default_id_raw};
            const auto default_id_utf8 = StringUtil::ToUTF8(default_id.get());
            for (auto& device : audio_devices) {
                device.default_device_ = device.id_ == default_id_utf8;
            }
        }
    }

    LOGI("event=audio_device.enumerate outcome=success count={}", audio_devices.size());
    return audio_devices;
}

}  // namespace px
