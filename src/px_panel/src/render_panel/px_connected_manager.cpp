#include "px_connected_manager.h"
#include <qapplication.h>
#include <Windows.h>
#include "px_context.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/log.h"
#include "px_app_messages.h"
#include "px_render_panel_message.pb.h"
#include "px_common_new/client_id_extractor.h"
#include "devices/connected_info_panel.h"
#include "devices/connected_info_tag.h"
#include "devices/connected_info_sliding_window.h"
#include "px_settings.h"
#include <QPointer>

namespace px { 
	PxConnectedManager::PxConnectedManager(const std::shared_ptr<PxContext>& ctx) : px_ctx_(ctx) {
		if (!px_ctx_) {
			LOGE("px_ctx_ is nullptr.");
			return;
		}

        CreatePanel();
        RegisterMessageListener();
        InitPanel();
	}

    PxConnectedManager::~PxConnectedManager() {
        if (msg_listener_) {
            msg_listener_->UnListenAll();
        }
        connected_info_panel_group_.clear();
    }

    bool PxConnectedManager::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_DISPLAYCHANGE) {
            if (px_ctx_) {
                QPointer<PxConnectedManager> self(this);
                px_ctx_->PostUIDelayTask([self]() {
                    if (!self) {
                        return;
                    }
                    //LOGI("nativeEventFilter WM_DISPLAYCHANGE");
                    self->AdjustPanelPosition();
                }, 4000);
            }
        }
        return false;
    }

    void PxConnectedManager::RegisterMessageListener() {
        msg_listener_ = px_ctx_->ObtainUIMessageListener();
        QPointer<PxConnectedManager> self(this);
        msg_listener_->Listen<MsgUpdateConnectedClientsInfo>([self](const MsgUpdateConnectedClientsInfo& msg) {
            if (!self || !self->px_ctx_) {
                LOGE("px_ctx_ is nullptr.");
                return;
            }

            self->client_connected_count_ = msg.clients_info_.size();

            int client_size = msg.clients_info_.size();
            if (0 == client_size) {
                self->HideAllPanels();
                return;
            }
            for (int index = 0; index < client_size; ++index) {
                auto client_info = msg.clients_info_[index];
                if (self->connected_info_panel_group_.count(index) > 0) {
                    self->connected_info_panel_group_[index]->show();
                    const std::string old_stream_id = self->connected_info_panel_group_[index]->GetStreamId();
                    self->connected_info_panel_group_[index]->UpdateInfo(client_info);
                    if (old_stream_id != client_info->stream_id()) {
                        self->connected_info_panel_group_[index]->Expand();
                    }
                }
            }

            int group_index = -1;
            for (auto& item : self->connected_info_panel_group_) {
                ++group_index;
                if (group_index < client_size) {
                    continue;
                }
                item.second->hide();
            }
        });

        msg_listener_->Listen<MsgOneClientDisconnect>([self](const MsgOneClientDisconnect&) {
            if (!self || !self->px_ctx_) {
                LOGE("px_ctx_ is nullptr.");
                return;
            }

            self->px_ctx_->PostUIDelayTask([self]() {
                if (!self) {
                    return;
                }
                auto settings = PxSettings::Instance();
                if (0 == self->client_connected_count_ && settings->IsDisconnectAutoLockScreenEnabled()) {
                    LockWorkStation();
                }
            }, 6000);
        });
    }

    void PxConnectedManager::TestShowPanel() {
        // test
    }

    void PxConnectedManager::AdjustPanelPosition() {
        auto primary_screen = QApplication::primaryScreen();
        if (!primary_screen) {
            return;
        }
        auto screen_rect = primary_screen->availableGeometry();
        int screen_width = screen_rect.width();
        int screen_height = screen_rect.height();
        int index = 0;
        for (auto& item: connected_info_panel_group_) {
            int panel_x = screen_width - item.second->width();
            int panel_y = screen_height - item.second->height() - 8 - item.first * item.second->height() * 1.1;
            item.second->move(panel_x, panel_y);
            //LOGI("index: {}, panel_x: {}, panel_y: {}", index, panel_x, panel_y);
            ++index;
        }
    }

    void PxConnectedManager::HideAllPanels() {
        for (auto& item : connected_info_panel_group_) {
            item.second->hide();
        }
    }

    void PxConnectedManager::ShowAllPanels() {
        for (auto& item : connected_info_panel_group_) {
            item.second->show();
        }
    }

    void PxConnectedManager::InitPanel() {
        AdjustPanelPosition();
    }

    void PxConnectedManager::CreatePanel() {
        connected_info_panel_group_.clear();
        const int kMaxCount = 2;
        for (int index = 0; index < kMaxCount; ++index) {
            auto sliding_window = std::make_unique<ConnectedInfoSlidingWindow>(px_ctx_);
            sliding_window->hide();
            connected_info_panel_group_[index] = std::move(sliding_window);
        }
    }
}
