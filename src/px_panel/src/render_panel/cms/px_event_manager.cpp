//
// Created by RGAA on 23/01/2026.
//

#include "px_event_manager.h"
#include "px_cms_client/cms_event.h"
#include "px_cms_client/cms_event_api.h"
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_application.h"
#include "render_panel/user/px_user_manager.h"
#include "px_common_new/log.h"

namespace px
{

    PxEventManager::PxEventManager(const std::shared_ptr<PxContext>& context) {
        context_ = context;
        settings_ = PxSettings::Instance();
    }

    bool PxEventManager::AddCpuEvent(int cpu_usage) {
        const auto host = settings_->GetCmsServerHost();
        const auto port = settings_->GetCmsServerPort();
        const auto appkey = grApp->GetAppkey();
        if (host.empty() || port <= 0 || appkey.empty()) {
            return false;
        }
        if (!user_mgr_) {
            user_mgr_ = grApp->GetUserManager();
            if (!user_mgr_) {
                LOGE("No user manager!");
                return false;
            }
        }

        std::string device_id = settings_->GetDeviceId();
        std::string device_ip = context_->GetFirstAvailableIp();
        std::string device_name = settings_->GetDeviceName();
        std::string uid = user_mgr_->GetUserId();
        std::string username = user_mgr_->GetUsername();
        const auto event = CmsEvent::CpuOverload(device_id, device_ip, device_name, uid, username, cpu_usage);
        if (const auto r = CmsEventApi::AddEvent(host, port, appkey, event); r.has_value()) {
            return true;
        }
        return false;
    }

    bool PxEventManager::AddMemoryEvent(int memory_usage) {
        const auto host = settings_->GetCmsServerHost();
        const auto port = settings_->GetCmsServerPort();
        const auto appkey = grApp->GetAppkey();
        if (host.empty() || port <= 0 || appkey.empty()) {
            return false;
        }
        if (!user_mgr_) {
            user_mgr_ = grApp->GetUserManager();
            if (!user_mgr_) {
                LOGE("No user manager!");
                return false;
            }
        }

        std::string device_id = settings_->GetDeviceId();
        std::string device_ip = context_->GetFirstAvailableIp();
        std::string device_name = settings_->GetDeviceName();
        std::string uid = user_mgr_->GetUserId();
        std::string username = user_mgr_->GetUsername();
        const auto event = CmsEvent::MemoryOverload(device_id, device_ip, device_name, uid, username, memory_usage);
        if (const auto r = CmsEventApi::AddEvent(host, port, appkey, event); r.has_value()) {
            return true;
        }
        return false;
    }

    bool PxEventManager::AddDiskEvent(int disk_usage, const std::string& disk_path) {
        const auto host = settings_->GetCmsServerHost();
        const auto port = settings_->GetCmsServerPort();
        const auto appkey = grApp->GetAppkey();
        if (host.empty() || port <= 0 || appkey.empty()) {
            return false;
        }
        if (!user_mgr_) {
            user_mgr_ = grApp->GetUserManager();
            if (!user_mgr_) {
                LOGE("No user manager!");
                return false;
            }
        }

        std::string device_id = settings_->GetDeviceId();
        std::string device_ip = context_->GetFirstAvailableIp();
        std::string device_name = settings_->GetDeviceName();
        std::string uid = user_mgr_->GetUserId();
        std::string username = user_mgr_->GetUsername();
        const auto event = CmsEvent::DiskOverload(device_id, device_ip, device_name, uid, username, disk_usage, disk_path);
        if (const auto r = CmsEventApi::AddEvent(host, port, appkey, event); r.has_value()) {
            return true;
        }
        return false;
    }

    bool PxEventManager::AddGpuEvent(int gpu_usage, const std::string& gpu_id, const std::string& gpu_name) {
        const auto host = settings_->GetCmsServerHost();
        const auto port = settings_->GetCmsServerPort();
        const auto appkey = grApp->GetAppkey();
        if (host.empty() || port <= 0 || appkey.empty()) {
            return false;
        }
        if (!user_mgr_) {
            user_mgr_ = grApp->GetUserManager();
            if (!user_mgr_) {
                LOGE("No user manager!");
                return false;
            }
        }

        std::string device_id = settings_->GetDeviceId();
        std::string device_ip = context_->GetFirstAvailableIp();
        std::string device_name = settings_->GetDeviceName();
        std::string uid = user_mgr_->GetUserId();
        std::string username = user_mgr_->GetUsername();
        const auto event = CmsEvent::GpuOverload(device_id, device_ip, device_name, uid, username, gpu_usage, gpu_id, gpu_name);
        if (const auto r = CmsEventApi::AddEvent(host, port, appkey, event); r.has_value()) {
            return true;
        }
        return false;
    }

}
