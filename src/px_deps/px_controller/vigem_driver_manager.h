//
// Created by RGAA on 2024/3/23.
//

#ifndef TC_APPLICATION_DRIVER_MANAGER_H
#define TC_APPLICATION_DRIVER_MANAGER_H

#include <memory>

namespace tc
{

    class Hardware;

    class VigemDriverManager {
    public:

        static std::shared_ptr<VigemDriverManager> Make();

        VigemDriverManager();
        bool Detect();
        bool IsVigemDriverInstalled() { return is_vigem_driver_installed_; }
        bool IsVJoyDriverInstalled() { return is_vjoy_driver_installed_; }

        bool TryConnect();
        void InstallViGem();

    private:
        std::shared_ptr<Hardware> hardware_ = nullptr;
        bool is_vigem_driver_installed_ = false;
        bool is_vjoy_driver_installed_ = false;
    };

}

#endif //TC_APPLICATION_DRIVER_MANAGER_H
