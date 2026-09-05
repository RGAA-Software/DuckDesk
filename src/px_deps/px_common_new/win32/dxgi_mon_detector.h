#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <d3d11.h>
#include <Dxgi1_6.h>
#include "px_common_new/log.h"

namespace px
{

    class DxgiMonInfo {
    public:
        DWORD LowPart{};
        LONG HighPart{};
        std::string display_name{};
        RECT rect{};
        int width{};
        int height{};
        bool primary{};

    public:
        [[nodiscard]] bool IsValid() const {
            return width > 0 && height > 0;
        }

        void Dump() const {
            LOGI("Monitor Info: {}, primary: {}, LowPart: {}, ({},{}), {}x{}",
                 display_name, primary, LowPart, rect.left, rect.top, width, height);
        }

    };

    class DxgiMonitorDetector {
    public:

        static DxgiMonitorDetector& Instance() {
            static DxgiMonitorDetector detector;
            return detector;
        }

        void DetectAdapters();
        [[nodiscard]] std::vector<DxgiMonInfo> GetAdapters() const;
        void PrintAdapters() const;
        [[nodiscard]] std::string GetNameById(DWORD lowpart) const;
        [[nodiscard]] DxgiMonInfo GetMonitorInfoByLowId(DWORD lowpart) const;

    private:
        mutable std::mutex mutex_{};
        std::vector<DxgiMonInfo> infos_{};

    };

}
