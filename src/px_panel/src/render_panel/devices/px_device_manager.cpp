//
// Created by RGAA on 27/11/2025.
//

#include "px_device_manager.h"
#include <format>
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "px_console_client/console_device.h"
#include "px_console_client/console_device_api.h"
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_application.h"
#include "px_label.h"
#include "px_dialog.h"

namespace px
{

    PxDeviceManager::PxDeviceManager(const std::shared_ptr<PxContext>& ctx)
        : settings_(*PxSettings::Instance()), context_(ctx) {}

    Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>
    PxDeviceManager::RequestNewDevice(
        const std::string& def_device_name,
        const std::string& info,
        const std::shared_ptr<std::atomic_bool>& cancellation) {
        auto host = settings_.get().GetConsoleServerHost();
        auto port = settings_.get().GetConsoleServerPort();
        auto appkey = grApp->GetAppkey();
        auto r = px_console::ConsoleDeviceApi::RequestNewDevice(
            host, port, appkey, def_device_name, info, cancellation);
        return r;
    }

    Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>
    PxDeviceManager::UpdateDesktopLink(
        const std::string& desktop_link,
        const std::string& desktop_link_raw,
        const std::shared_ptr<std::atomic_bool>& cancellation) {
        auto host = settings_.get().GetConsoleServerHost();
        auto port = settings_.get().GetConsoleServerPort();
        auto appkey = grApp->GetAppkey();
        auto device_id = settings_.get().GetDeviceId();
        auto r = px_console::ConsoleDeviceApi::UpdateDesktopLink(
            host, port, appkey, device_id, desktop_link, desktop_link_raw, cancellation);
        if (r.has_value()) {
            LOGI("UpdateDesktopLink success for device: {} ", device_id);
        }
        else {
            auto err = r.error();
            LOGE("UpdateDesktop link failed, err: {}, msg: {}", (int)err, px_console::ConsoleApiErrorAsString(err));
        }
        return r;
    }

    Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>
    PxDeviceManager::UpdateDeviceName(
        const std::string& device_name,
        const std::shared_ptr<std::atomic_bool>& cancellation) {
        auto host = settings_.get().GetConsoleServerHost();
        auto port = settings_.get().GetConsoleServerPort();
        auto appkey = grApp->GetAppkey();
        auto device_id = settings_.get().GetDeviceId();
        auto r = px_console::ConsoleDeviceApi::UpdateDeviceName(
            host, port, appkey, device_id, device_name, cancellation);
        return r;
    }

    Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>
    PxDeviceManager::UpdateUsedTime(
        int period,
        const std::shared_ptr<std::atomic_bool>& cancellation) {
        auto host = settings_.get().GetConsoleServerHost();
        auto port = settings_.get().GetConsoleServerPort();
        auto appkey = grApp->GetAppkey();
        auto device_id = settings_.get().GetDeviceId();
        auto r = px_console::ConsoleDeviceApi::UpdateUsedTime(
            host, port, appkey, device_id, period, cancellation);
        return r;
    }

    Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>
    PxDeviceManager::QueryDevice(
        const std::string& device_id,
        const std::shared_ptr<std::atomic_bool>& cancellation) {
        return px_console::ConsoleDeviceApi::QueryDevice(
            settings_.get().GetConsoleServerHost(),
            settings_.get().GetConsoleServerPort(),
            grApp->GetAppkey(),
            device_id,
            cancellation);
    }

    Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>
    PxDeviceManager::UpdateRandomPassword(
        const std::shared_ptr<std::atomic_bool>& cancellation) {
        return px_console::ConsoleDeviceApi::UpdateRandomPwd(
            settings_.get().GetConsoleServerHost(),
            settings_.get().GetConsoleServerPort(),
            grApp->GetAppkey(),
            settings_.get().GetDeviceId(),
            cancellation);
    }

}
