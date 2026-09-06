//
// Created by RGAA on 2024/3/23.
//

#ifndef TC_APPLICATION_VIGEM_CONTROLLER_H
#define TC_APPLICATION_VIGEM_CONTROLLER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <Windows.h>
#include "sdk/ViGEm/Client.h"
#include "vigem_defs.h"

namespace px
{

    enum class JoystickType {
        kJsX360,
        kJsDs4
    };

    class VigemController {
    public:
        using RumbleCallback = std::function<void(const std::string&, std::uint8_t, std::uint8_t)>;

        explicit VigemController(const JoystickType& js_type, RumbleCallback rumble_callback = {});
        ~VigemController();
        bool Connect();
        bool IsConnected();
        bool AllocController(const std::string& stream_id);
        bool RemoveController(const std::string& stream_id);
        void SendGamepadState(const std::string& stream_id, const XInputGamepadState& state);
        void Exit();

        void MockPressB();

    private:
        struct ClientDeleter final {
            void operator()(std::remove_pointer_t<PVIGEM_CLIENT>* client) const noexcept; // NOLINT(gammaray-raw-pointer-boundary): ViGEm C handle boundary
        };
        struct TargetDeleter final {
            void operator()(std::remove_pointer_t<PVIGEM_TARGET>* target) const noexcept; // NOLINT(gammaray-raw-pointer-boundary): ViGEm C handle boundary
        };
        using ClientHandle = std::unique_ptr<
            std::remove_pointer_t<PVIGEM_CLIENT>, ClientDeleter>;
        using TargetHandle = std::unique_ptr<
            std::remove_pointer_t<PVIGEM_TARGET>, TargetDeleter>;

        JoystickType js_type_;
        RumbleCallback rumble_callback_;
        ClientHandle client_;
        std::unordered_map<std::string, TargetHandle> targets_;
    };

}

#endif //TC_APPLICATION_VIGEM_CONTROLLER_H
