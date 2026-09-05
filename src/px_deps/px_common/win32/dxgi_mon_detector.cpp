#include "dxgi_mon_detector.h"
#include "px_common/log.h"
#include "px_common/string_util.h"
#include <Windows.h>
#include <wrl/client.h>

#pragma comment(lib, "DXGI.lib")
#pragma comment(lib, "D3D11.lib")

namespace px
{

    void DxgiMonitorDetector::DetectAdapters() {
        std::vector<DxgiMonInfo> detected;

        // Get primary monitor info using Win32 API
        POINT pt{};
        HMONITOR hPrimary = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);  // NOLINT(gammaray-raw-pointer-boundary): borrowed monitor handle.
        MONITORINFO primaryMi = { sizeof(MONITORINFO) };
        RECT primaryRect = {};
        if (hPrimary && GetMonitorInfoW(hPrimary, &primaryMi)) {
            primaryRect = primaryMi.rcMonitor;
        } else {
            LOGE("Get primary monitor info failed.");
        }

        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        const HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr)) {
            LOGE("CreateDXGIFactory failed.");
            return;
        }
        int max_devices = 16;

        for (int i = 0; i < max_devices; i++) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(i, adapter.GetAddressOf()) != S_OK) {
                break;
            }

            for (int j = 0; j < max_devices; j++) {
                Microsoft::WRL::ComPtr<IDXGIOutput> dxgi_output;
                const auto r = adapter->EnumOutputs(j, dxgi_output.GetAddressOf());
                if (dxgi_output && SUCCEEDED(r)) {

                    DxgiMonInfo info{};
                    DXGI_ADAPTER_DESC adapterDesc{};
                    adapter->GetDesc(&adapterDesc);
                    info.HighPart = adapterDesc.AdapterLuid.HighPart;
                    info.LowPart = adapterDesc.AdapterLuid.LowPart;

                    DXGI_OUTPUT_DESC desc{};
                    dxgi_output->GetDesc(&desc);
                    info.display_name = StringUtil::ToUTF8(desc.DeviceName);
                    info.rect = desc.DesktopCoordinates;
                    info.width = info.rect.right - info.rect.left;
                    info.height = info.rect.bottom - info.rect.top;
                    //LOGI("Monitor detect: {} => {} , primary screen: {}", j, info.display_name, primary_screen->name().toStdString());
                    //LOGI("Monitor position: ({},{}), {}x{}", info.rect.left, info.rect.top, info.width, info.height);
                    if (info.rect.left == primaryRect.left && info.rect.top == primaryRect.top
                        && info.width == (primaryRect.right - primaryRect.left)
                        && info.height == (primaryRect.bottom - primaryRect.top)) {
                        info.primary = true;
                        //LOGI("Bingo...Primary monitor is : {}", info.display_name);
                    }
                    else {
                        info.primary = false;
                    }
                    detected.push_back(info);
                }
            }
        }
        std::lock_guard lock(mutex_);
        infos_ = std::move(detected);
    }

    std::vector<DxgiMonInfo> DxgiMonitorDetector::GetAdapters() const {
        std::lock_guard lock(mutex_);
        return infos_;
    }

    void DxgiMonitorDetector::PrintAdapters() const {
        for (const auto& mon : GetAdapters()) {
            mon.Dump();
        }
    }

    std::string DxgiMonitorDetector::GetNameById(DWORD lowpart) const {
        std::lock_guard lock(mutex_);
        for (const auto& info : infos_) {
            if (info.LowPart == lowpart) {
                return info.display_name;
            }
        }
        return "";
    }

    DxgiMonInfo DxgiMonitorDetector::GetMonitorInfoByLowId(DWORD lowpart) const {
        std::lock_guard lock(mutex_);
        DxgiMonInfo empty_info{};
        for (const auto& info : infos_) {
            if (info.LowPart == lowpart) {
                return info;
            }
        }
        return empty_info;
    }

}
