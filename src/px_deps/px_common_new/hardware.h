//
// Created by RGAA on 2024-01-18.
//

#ifndef CONTROLLER_HARDWARE_H
#define CONTROLLER_HARDWARE_H

#include <string>
#include <vector>

namespace tc
{

    class HwCPU {
    public:
        std::string name_;
        std::string id_;
        uint32_t num_cores_ = 0;
        uint32_t max_clock_speed_;
    };

    class HwDisk {
    public:
        std::string name_;
        std::string model_;
        std::string status_;
        std::string serial_number_;
        std::string interface_type_;
    };

    class SysDriver {
    public:
        std::string name_;
        std::string display_name_;
        std::string state_;
    };

    class HwGPU {
    public:
        //
        std::string name_;
        // bytes
        unsigned long long size_{0};
        std::string size_str_;
        // res wxh
        int res_w_{0};
        int res_h_{0};
        std::string driver_version_;
        std::string pnp_device_id_;

    };

    class Hardware {
    public:
        static Hardware* Instance() {
            static Hardware hw;
            return &hw;
        }

        int Detect(bool cpu, bool disk, bool driver);
        void Dump();
        std::string GetHardwareDescription();
        std::vector<SysDriver> GetDrivers() { return drivers_; }

        static std::string GetDesktopName();

        static void LockScreen();
        static void RestartDevice();
        static void ShutdownDevice();
        static bool AcquirePermissionForRestartDevice();
    private:
        void DetectMac();

    public:
        HwCPU hw_cpu_;
        std::vector<HwDisk> hw_disks_;
        std::vector<SysDriver> drivers_;
        std::string mac_address_;
        std::string desktop_name_;
        size_t memory_size_;
        std::vector<HwGPU> gpus_;
    };

}

#endif //PLC_CONTROLLER_HARDWARE_H
