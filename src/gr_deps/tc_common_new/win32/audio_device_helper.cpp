//
// Created by RGAA on 6/08/2024.
//

#include "audio_device_helper.h"
#include <Windows.h>
#include <Mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>
#include "tc_common_new/string_util.h"

namespace tc
{

    std::vector<AudioDevice> AudioDeviceHelper::DetectAudioDevices() {
        HRESULT hr = S_OK;
        IMMDeviceEnumerator *pEnumerator = nullptr;
        IMMDeviceCollection *pDeviceCollection = nullptr;

        // 初始化 COM
        hr = CoInitialize(nullptr);
        if (FAILED(hr)) {
            printf("COM initialization failed\n");
            return {};
        }

        // 创建设备枚举器
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (LPVOID*)&pEnumerator);
        if (FAILED(hr)) {
            printf("Failed to create device enumerator\n");
            CoUninitialize();
            return {};
        }

        // 获取所有音频渲染设备
        hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pDeviceCollection);
        if (FAILED(hr)) {
            printf("Failed to enumerate audio endpoints\n");
            pEnumerator->Release();
            CoUninitialize();
            return {};
        }

        UINT deviceCount = 0;
        hr = pDeviceCollection->GetCount(&deviceCount);
        if (FAILED(hr)) {
            printf("Failed to get device count\n");
            pDeviceCollection->Release();
            pEnumerator->Release();
            CoUninitialize();
            return {};
        }

        std::vector<AudioDevice> audio_devices;

        for (UINT i = 0; i < deviceCount; ++i) {
            IMMDevice *pDevice = nullptr;
            LPWSTR deviceId = nullptr;
            LPWSTR deviceName = nullptr;

            AudioDevice audio_device{};

            hr = pDeviceCollection->Item(i, &pDevice);
            if (FAILED(hr)) {
                printf("Failed to get device %d\n", i);
                continue;
            }

            hr = pDevice->GetId(&deviceId);
            if (FAILED(hr)) {
                pDevice->Release();
                continue;
            }

            audio_device.id_ = StringUtil::ToUTF8(deviceId);

            IPropertyStore *pPropertyStore = nullptr;
            hr = pDevice->OpenPropertyStore(STGM_READ, &pPropertyStore);
            if (FAILED(hr)) {
                pDevice->Release();
                continue;
            }

            PROPVARIANT varName;
            PropVariantInit(&varName);
            hr = pPropertyStore->GetValue(PKEY_Device_FriendlyName, &varName);
            if (SUCCEEDED(hr)) {
                deviceName = varName.pwszVal;
                wprintf(L"Device Name: %s\n", deviceName);
            }
            audio_device.name_ = StringUtil::ToUTF8(deviceName);

            PropVariantClear(&varName);
            pPropertyStore->Release();
            CoTaskMemFree(deviceId);
            pDevice->Release();

            audio_devices.push_back(audio_device);
        }

        // 获取默认的音频输出设备
        IMMDevice *pDefaultDevice = nullptr;
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDefaultDevice);
        if (SUCCEEDED(hr)) {
            LPWSTR defaultDeviceId = nullptr;
            hr = pDefaultDevice->GetId(&defaultDeviceId);
            if (SUCCEEDED(hr) && defaultDeviceId) {
                const auto default_id_utf8 = StringUtil::ToUTF8(defaultDeviceId);
                wprintf(L"\nDefault Audio Output Device ID: %s\n", defaultDeviceId);
                for (auto& device : audio_devices) {
                    if (device.id_ == default_id_utf8) {
                        device.default_device_ = true;
                    }
                }
                CoTaskMemFree(defaultDeviceId);
            }
            pDefaultDevice->Release();
        }

        pDeviceCollection->Release();
        pEnumerator->Release();
        CoUninitialize();

        return audio_devices;
    }

}