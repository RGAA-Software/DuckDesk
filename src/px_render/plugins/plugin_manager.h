//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_PLUGIN_MANAGER_H
#define PX_RENDER_PLUGIN_MANAGER_H

#include <memory>
#include <string>
#include <map>
#include <atomic>
#include <shared_mutex>
#include "plugin_ids.h"
#include "px_common_new/win32/dynamic_library.h"
#include "px_common_new/concurrent_hashmap.h"
#include "px_render/plugin_interface/px_plugin_settings_info.h"

namespace px
{

    class RdContext;
    class RdSettings;
    class RdApplication;
    class PluginEventRouter;
    class PxPluginInterface;
    class PxStreamPlugin;
    class PxVideoEncoderPlugin;
    class PxNetPlugin;
    class PxMonitorCapturePlugin;
    class PxDataProviderPlugin;
    class PxAudioEncoderPlugin;
    class PxFrameCarrierPlugin;
    class PxFrameProcessorPlugin;
    class PxConnectedClientInfo;

    class PluginManager : public std::enable_shared_from_this<PluginManager> {
    public:
        static std::shared_ptr<PluginManager> Make(const std::shared_ptr<RdApplication>& app);

        explicit PluginManager(const std::shared_ptr<RdApplication>& app);
        ~PluginManager();

        void LoadAllPlugins();
        void RegisterPluginEventsCallback();
        void ReleaseAllPlugins();
        void ReleasePlugin(const std::string& name);

        PxPluginInterface* GetPluginById(const std::string& id);
        PxVideoEncoderPlugin* GetFFmpegEncoderPlugin();
        PxVideoEncoderPlugin* GetNvencEncoderPlugin();
        PxVideoEncoderPlugin* GetAmfEncoderPlugin();
        PxMonitorCapturePlugin* GetDDACapturePlugin();
        PxMonitorCapturePlugin* GetGdiCapturePlugin();
        PxDataProviderPlugin* GetMockVideoStreamPlugin();
        PxDataProviderPlugin* GetAudioCapturePlugin();
        PxAudioEncoderPlugin* GetAudioEncoderPlugin();
        PxPluginInterface* GetClipboardPlugin();
        PxPluginInterface* GetRtcPlugin();
        PxNetPlugin* GetUdpPlugin();
        PxNetPlugin* GetRelayPlugin();
        PxFrameCarrierPlugin* GetFrameCarrierPlugin();
        PxFrameProcessorPlugin* GetFrameResizePlugin();
        PxPluginInterface* GetEventsReplayerPlugin();
        PxPluginInterface* GetRtcLocalPlugin();
        int64_t GetQueuingMediaMsgCountInNetPlugins();
        int64_t GetQueuingFtMsgCountInNetPlugins();
        int GetTotalConnectedClientsCount();
        std::vector<std::shared_ptr<PxConnectedClientInfo>> GetConnectedClientsInfo();

        void VisitAllPlugins(const std::function<void(PxPluginInterface*)>&& visitor);
        void VisitStreamPlugins(const std::function<void(PxStreamPlugin*)>&& visitor);
        void VisitUtilPlugins(const std::function<void(PxPluginInterface*)>&& visitor);
        void VisitEncoderPlugins(const std::function<void(PxVideoEncoderPlugin*)>&& visitor);
        void VisitNetPlugins(const std::function<void(PxNetPlugin*)>&& visitor);
        void DumpPluginInfo();

        void On1Second();

        // from render panel -> render
        void SyncPluginSettingsInfo(const PxPluginSettingsInfo& info);

        // is GDI
        bool IsGDIMonitorCapturePlugin(PxMonitorCapturePlugin* plugin);
        // is DDA
        bool IsDDAMonitorCapturePlugin(PxMonitorCapturePlugin* plugin);

    private:
        RdSettings* settings_ = nullptr;
        std::shared_ptr<RdApplication> app_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        // guards plugins_: visitors take a shared lock, ReleaseAllPlugins takes an exclusive one
        std::shared_mutex plugins_mtx_;
        std::map<std::string, PxPluginInterface*> plugins_;
        std::map<std::string, std::shared_ptr<DynamicLibrary>> plugin_libraries_;
        std::shared_ptr<PluginEventRouter> evt_router_ = nullptr;
        std::atomic_bool exiting_ = false;
    };

}

#endif //PX_RENDER_PLUGIN_MANAGER_H
