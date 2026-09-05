#ifndef CONTROLLER_HARDWARE_H
#define CONTROLLER_HARDWARE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace px {

struct HwCPU {
    std::string name_{};
    std::string id_{};
    std::uint32_t num_cores_{0};
    std::uint32_t max_clock_speed_{0};
};

struct HwDisk {
    std::string name_{};
    std::string model_{};
    std::string status_{};
    std::string serial_number_{};
    std::string interface_type_{};
};

struct SysDriver {
    std::string name_{};
    std::string display_name_{};
    std::string state_{};
};

struct HwGPU {
    std::string name_{};
    std::uint64_t size_{0};
    std::string size_str_{};
    int res_w_{0};
    int res_h_{0};
    std::string driver_version_{};
    std::string pnp_device_id_{};
};

class Hardware final {
public:
    static Hardware& Instance();

    int Detect(bool cpu, bool disk, bool driver);
    void Dump() const;
    [[nodiscard]] std::string GetHardwareDescription() const;
    [[nodiscard]] std::vector<SysDriver> GetDrivers() const { return drivers_; }

    static std::string GetDesktopName();
    static void LockScreen();
    static void RestartDevice();
    static void ShutdownDevice();
    static bool AcquirePermissionForRestartDevice();

    HwCPU hw_cpu_{};
    std::vector<HwDisk> hw_disks_{};
    std::vector<SysDriver> drivers_{};
    std::string mac_address_{};
    std::string desktop_name_{};
    std::size_t memory_size_{0};
    std::vector<HwGPU> gpus_{};

private:
    void DetectMac();
};

}  // namespace px

#endif  // CONTROLLER_HARDWARE_H
