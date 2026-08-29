//
// Created by RGAA on 22/05/2025.
//

#include "ct_plugin_interface.h"
#include "ct_plugin_context.h"
#include "px_common_new/image.h"
#include "px_common_new/data.h"
#include "px_common_new/thread.h"
#include "px_common_new/log.h"
#include "px_common_new/snowflake_id.h"
#include <QtCore/QEvent>
#include <px_common_new/string_util.h>

namespace px
{

    class ClientPluginEventChannel {
    public:
        void Register(const ClientPluginEventCallback& callback) {
            std::lock_guard lock(mutex_);
            callback_ = callback;
            accepting_ = static_cast<bool>(callback_);
        }

        void Deactivate() {
            std::lock_guard lock(mutex_);
            accepting_ = false;
            callback_ = nullptr;
        }

        [[nodiscard]] bool CanDeliver() const {
            std::lock_guard lock(mutex_);
            return accepting_ && static_cast<bool>(callback_);
        }

        void Deliver(const std::shared_ptr<ClientPluginBaseEvent>& event) const {
            ClientPluginEventCallback callback;
            {
                std::lock_guard lock(mutex_);
                if (!accepting_ || !callback_) {
                    return;
                }
                callback = callback_;
            }
            callback(event);
        }

    private:
        mutable std::mutex mutex_;
        ClientPluginEventCallback callback_;
        bool accepting_ = false;
    };

    ClientPluginInterface::ClientPluginInterface()
        : event_channel_(std::make_shared<ClientPluginEventChannel>()) {
    }

    std::shared_ptr<ClientPluginContext> ClientPluginInterface::GetPluginContext() {
        return plugin_context_;
    }

    std::string ClientPluginInterface::GetPluginName() {
        return "dummy";
    }

    std::string ClientPluginInterface::GetPluginAuthor() {
        return plugin_author_;
    }

    std::string ClientPluginInterface::GetPluginDescription() {
        return "plugin description";
    }

    ClientPluginType ClientPluginInterface::GetPluginType() {
        return plugin_type_;
    }

    std::string ClientPluginInterface::GetVersionName() {
        return "1.0.0";
    }

    uint32_t ClientPluginInterface::GetVersionCode() {
        return 1;
    }

    bool ClientPluginInterface::IsPluginEnabled() {
        return plugin_enabled_;
    }

    void ClientPluginInterface::EnablePlugin() {
        plugin_enabled_ = true;
    }

    void ClientPluginInterface::DisablePlugin() {
        plugin_enabled_ = false;
    }

    bool ClientPluginInterface::IsWorking() {
        return false;
    }

    bool ClientPluginInterface::OnCreate(const ClientPluginParam& param) {
        if (lifecycle_state_.load() == ClientPluginLifecycleState::Running) {
            return true;
        }
        if (lifecycle_state_.load() == ClientPluginLifecycleState::Destroyed) {
            return false;
        }
        SnowflakeId::initialize(0, 105);
        this->param_ = param;
        if (param.cluster_.contains("name")) {
            auto n = param.cluster_.at("name");
            plugin_file_name_ = std::any_cast<std::string>(n);
        }

        if (param.cluster_.contains("base_path")) {
            base_path_ = std::any_cast<std::string>(param.cluster_.at("base_path"));
        }
        std::wstring base_data_path;
        if (param.cluster_.contains("base_data_path")) {
            base_data_path = std::any_cast<std::wstring>(param.cluster_.at("base_data_path"));
        }
        plugin_context_ = std::make_shared<ClientPluginContext>(GetPluginName());
        const auto log_path = std::format(L"{}/px_logs/ct_{}.log", base_data_path, StringUtil::ToWString(plugin_file_name_));
        Logger::InitLog(log_path, true);
        LOGI("{} OnCreate", GetPluginName());

        capture_audio_device_id_ = GetConfigParam<std::string>("capture_audio_device_id");

        screen_recording_path_ = GetConfigParam<std::string>("screen_recording_path");

        plugin_settings_.clipboard_enabled_ = GetConfigBoolParam("clipboard_enabled");
        plugin_settings_.device_id_ = GetConfigStringParam("device_id");
        plugin_settings_.stream_id_ = GetConfigStringParam("stream_id");
        plugin_settings_.language_ = (int)GetConfigIntParam("language");
        plugin_settings_.stream_name_ = GetConfigStringParam("stream_name");
        plugin_settings_.display_name_ = GetConfigStringParam("display_name");
        plugin_settings_.display_remote_name_ = GetConfigStringParam("display_remote_name");

        LOGI("plugin settings clipboard enabled: {}", plugin_settings_.clipboard_enabled_);
        LOGI("plugin settings device id: {}", plugin_settings_.device_id_);
        LOGI("plugin settings stream id: {}", plugin_settings_.stream_id_);

        // print params
        LOGI("Input params size : {}", param.cluster_.size());
        for (const auto& [key, value]: param.cluster_) {
            if (value.type() == typeid(std::string)) {
                const auto lower_key = StringUtil::ToLowerCpy(key);
                const bool sensitive = lower_key.find("appkey") != std::string::npos
                    || lower_key.find("ticket") != std::string::npos
                    || lower_key.find("token") != std::string::npos
                    || lower_key.find("nonce") != std::string::npos
                    || lower_key.find("password") != std::string::npos
                    || lower_key.find("pwd") != std::string::npos;
                LOGI(" * {} => {}", key,
                     sensitive ? "<redacted>" : std::any_cast<std::string>(value));
            }
            else if (value.type() == typeid(int64_t)) {
                LOGI(" * {} => {}", key, std::any_cast<int64_t>(value));
            }
            else if (value.type() == typeid(double)) {
                LOGI(" * {} => {}", key, std::any_cast<double>(value));
            }
            else if (value.type() == typeid(bool)) {
                LOGI(" * {} => {}", key, std::any_cast<bool>(value));
            }
        }

        root_widget_ = std::make_unique<QWidget>(nullptr, Qt::Window);
        root_widget_->resize(960, 540);
        root_widget_->hide();
        root_widget_->installEventFilter(this);

        stopped_ = false;
        destroyed_ = false;
        lifecycle_state_ = ClientPluginLifecycleState::Running;
        return true;
    }

    bool ClientPluginInterface::OnResume() {
        if (lifecycle_state_.load() == ClientPluginLifecycleState::Destroyed) {
            return false;
        }
        this->stopped_ = false;
        lifecycle_state_ = ClientPluginLifecycleState::Running;
        return true;
    }

    bool ClientPluginInterface::OnStop() {
        if (lifecycle_state_.load() == ClientPluginLifecycleState::Destroyed) {
            return true;
        }
        lifecycle_state_ = ClientPluginLifecycleState::Stopping;
        this->stopped_ = true;
        event_channel_->Deactivate();
        event_cbk_ = nullptr;
        return true;
    }

    bool ClientPluginInterface::OnDestroy() {
        if (lifecycle_state_.load() == ClientPluginLifecycleState::Destroyed) {
            return true;
        }
        lifecycle_state_ = ClientPluginLifecycleState::Stopping;
        stopped_ = true;
        event_channel_->Deactivate();
        event_cbk_ = nullptr;
        if (root_widget_) {
            root_widget_->removeEventFilter(this);
            root_widget_->hide();
            root_widget_->close();
            root_widget_.reset();
        }
        if (plugin_context_) {
            plugin_context_->OnDestroy();
            plugin_context_.reset();
        }
        destroyed_ = true;
        lifecycle_state_ = ClientPluginLifecycleState::Destroyed;
        return true;
    }

    bool ClientPluginInterface::IsStoppingOrDestroyed() const {
        auto state = lifecycle_state_.load();
        return stopped_ || destroyed_
            || state == ClientPluginLifecycleState::Stopping
            || state == ClientPluginLifecycleState::Destroyed;
    }

    void ClientPluginInterface::PostWorkTask(std::function<void()>&& task) {
        if (plugin_context_ && !IsStoppingOrDestroyed()) {
            plugin_context_->PostWorkTask(std::move(task));
        }
    }

    void ClientPluginInterface::PostUITask(std::function<void()>&& task) {
        if (plugin_context_ && !IsStoppingOrDestroyed()) {
            plugin_context_->PostUITask(std::move(task));
        }
    }

    void ClientPluginInterface::PostUIDelayTask(int ms, std::function<void()>&& task) {
        if (plugin_context_ && !IsStoppingOrDestroyed()) {
            plugin_context_->PostDelayTask(std::move(task), ms);
        }
    }

    void ClientPluginInterface::RegisterEventCallback(const ClientPluginEventCallback& cbk) {
        event_cbk_ = cbk;
        event_channel_->Register(cbk);
    }

    void ClientPluginInterface::CallbackEvent(const std::shared_ptr<ClientPluginBaseEvent>& event) {
        if (!event || IsStoppingOrDestroyed() || !event_channel_->CanDeliver()) {
            return;
        }
        const auto weak_channel = std::weak_ptr<ClientPluginEventChannel>(event_channel_);
        PostWorkTask([weak_channel, event]() {
            if (const auto channel = weak_channel.lock()) {
                channel->Deliver(event);
            }
        });
    }

    void ClientPluginInterface::CallbackEventDirectly(const std::shared_ptr<ClientPluginBaseEvent>& event) {
        if (event && !IsStoppingOrDestroyed()) {
            event_channel_->Deliver(event);
        }
    }

    ClientPluginEventCallback ClientPluginInterface::MakeDirectEventDispatcher() const {
        const auto weak_channel =
            std::weak_ptr<ClientPluginEventChannel>(event_channel_);
        return [weak_channel](
            const std::shared_ptr<ClientPluginBaseEvent>& event) {
            if (!event) {
                return;
            }
            if (const auto channel = weak_channel.lock()) {
                channel->Deliver(event);
            }
        };
    }

    ClientPluginEventCallback ClientPluginInterface::MakeQueuedEventDispatcher() const {
        const auto weak_context =
            std::weak_ptr<ClientPluginContext>(plugin_context_);
        const auto weak_channel =
            std::weak_ptr<ClientPluginEventChannel>(event_channel_);
        return [weak_context, weak_channel](
            const std::shared_ptr<ClientPluginBaseEvent>& event) {
            if (!event) {
                return;
            }
            const auto context = weak_context.lock();
            if (!context) {
                return;
            }
            context->PostWorkTask([weak_channel, event]() {
                if (const auto channel = weak_channel.lock()) {
                    channel->Deliver(event);
                }
            });
        };
    }

    void ClientPluginInterface::On1Second() {

    }

    QWidget* ClientPluginInterface::GetRootWidget() {
        return root_widget_.get(); // NOLINT(gammaray-raw-pointer-boundary): Qt widget observer ABI
    }

    bool ClientPluginInterface::eventFilter(QObject *watched, QEvent *event) {
        if (watched == root_widget_.get()) {
            if (event->type() == QEvent::Type::Close) {
                LOGI("Event: {}", (int) event->type());
                event->ignore();
                root_widget_->hide();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

    void ClientPluginInterface::ShowRootWidget() {
        root_widget_->show();
    }

    void ClientPluginInterface::HideRootWidget() {
        root_widget_->hide();
    }

    void ClientPluginInterface::OnMessage(std::shared_ptr<Message> msg) {

    }

    void ClientPluginInterface::OnMessageRaw(const std::any& msg) {

    }

    void ClientPluginInterface::SyncClientPluginSettings(const ClientPluginSettings& st) {
        plugin_settings_.clipboard_enabled_ = st.clipboard_enabled_;
    }

    ClientPluginSettings ClientPluginInterface::GetPluginSettings() {
        return plugin_settings_;
    }

    bool ClientPluginInterface::HasProcessingTasks() {
        return false;
    }

}
