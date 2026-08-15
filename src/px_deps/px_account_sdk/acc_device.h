//
// Created by RGAA on 30/05/2025.
//

#ifndef GAMMARAY_ACC_DEVICE_H
#define GAMMARAY_ACC_DEVICE_H

#include <string>
#include <cstdint>

namespace px
{

    // device information in User Server
    class AccountDevice {
    public:
        [[nodiscard]] bool IsValid() const {
            return !device_id_.empty();
        }

    public:
        std::string device_id_;
        std::string device_desktop_name_;
        std::string device_os_;
        std::string device_custom_name_;
    };

}

#endif //GAMMARAY_ACC_DEVICE_H
