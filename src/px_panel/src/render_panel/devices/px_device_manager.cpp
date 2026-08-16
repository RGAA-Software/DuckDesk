//
// Created by RGAA on 27/11/2025.
//

#include "px_device_manager.h"
#include <format>
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "px_cms_client/cms_device.h"
#include "px_cms_client/cms_device_api.h"
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_application.h"
#include "px_label.h"
#include "px_dialog.h"

namespace px
{

    PxDeviceManager::PxDeviceManager(const std::shared_ptr<PxContext>& ctx) {
        context_ = ctx;
        settings_ = PxSettings::Instance();
    }

    Result<std::shared_ptr<px_cms::CmsDevice>, px_cms::CmsApiError> PxDeviceManager::RequestNewDevice(const std::string& def_device_name, const std::string& info) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto r = px_cms::CmsDeviceApi::RequestNewDevice(host, port, appkey, def_device_name, info);
        return r;
    }

    bool PxDeviceManager::UpdateDesktopLink(const std::string& desktop_link, const std::string& desktop_link_raw) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto device_id = settings_->GetDeviceId();
        auto r = px_cms::CmsDeviceApi::UpdateDesktopLink(host, port, appkey, device_id, desktop_link, desktop_link_raw);
        if (r.has_value()) {
            LOGI("UpdateDesktopLink success for device: {} ", device_id);
            return true;
        }
        else {
            auto err = r.error();
            LOGE("UpdateDesktop link failed, err: {}, msg: {}", (int)err, px_cms::CmsApiErrorAsString(err));
            return false;
        }
    }

    Result<std::shared_ptr<px_cms::CmsDevice>, px_cms::CmsApiError> PxDeviceManager::UpdateDeviceName(const std::string& device_name) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto device_id = settings_->GetDeviceId();
        auto r = px_cms::CmsDeviceApi::UpdateDeviceName(host, port, appkey, device_id, device_name);
        return r;
    }

    Result<std::shared_ptr<px_cms::CmsDevice>, px_cms::CmsApiError> PxDeviceManager::UpdateUsedTime(int period) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto device_id = settings_->GetDeviceId();
        auto r = px_cms::CmsDeviceApi::UpdateUsedTime(host, port, appkey, device_id, period);
        return r;
    }

    Result<std::shared_ptr<px_cms::CmsDevice>, px_cms::CmsApiError> PxDeviceManager::QueryDevice(const std::string& device_id) {
        return px_cms::CmsDeviceApi::QueryDevice(settings_->GetCmsServerHost(),
                                                settings_->GetCmsServerPort(),
                                                grApp->GetAppkey(),
                                                device_id);
    }

}