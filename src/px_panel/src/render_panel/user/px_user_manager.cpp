//
// Created by RGAA on 18/11/2025.
//

#include "px_user_manager.h"
#include <format>
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "px_cms_client/cms_user.h"
#include "px_cms_client/cms_user_api.h"
#include "px_cms_client/cms_user_device_api.h"
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_application.h"
#include "render_panel/px_app_messages.h"
#include "px_label.h"
#include "px_dialog.h"

const std::string kUserPrefix = "cms_user:";

namespace px
{

    GrUserManager::GrUserManager(const std::shared_ptr<GrContext>& ctx) {
        context_ = ctx;
        settings_ = GrSettings::Instance();
    }

    bool GrUserManager::Register(const std::string& username, const std::string& password) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto hash_password = MD5::Hex(password);
        auto r = px_cms::CmsUserApi::Register(host, port, appkey, username, hash_password);
        if (r.has_value()) {
            auto user = r.value();
            this->SaveUserInfo(user->uid_, user->username_, password, user->avatar_path_);
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_register_success"));
            LOGI("Register success");
        }
        else {
            auto err = r.error();
            LOGE("Register failed, err: {}, msg: {}", (int)err, px_cms::CmsApiErrorAsString(err));
            context_->PostUITask([=, this]() {
                QString msg = tcTr("id_op_error") + ":" + QString::number((int)err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
                TcDialog dialog(tcTr("id_error"), msg);
                dialog.exec();
            });
        }
        return true;
    }

    bool GrUserManager::Login(const std::string& username, const std::string& password, bool show_dialog) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto hash_password = MD5::Hex(password);

        auto device_id = settings_->GetDeviceId();
        if (device_id.empty()) {
            if (show_dialog) {
                context_->PostUITask([=, this]() {
                    QString msg = tcTr("id_unmanaged_device");
                    TcDialog dialog(tcTr("id_error"), msg);
                    dialog.exec();
                });
            }
            return false;
        }

        auto r = px_cms::CmsUserApi::Login(host, port, appkey, username, hash_password, device_id);
        if (r.has_value()) {
            auto user = r.value();
            this->SaveUserInfo(user->uid_, user->username_, password, user->avatar_path_);
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_login_success"));

            // send a logged in message
            context_->SendAppMessage(MsgUserLoggedIn {});

            return true;
        }
        else {
            auto err = r.error();
            LOGE("Register failed, err: {}, msg: {}", (int)err, px_cms::CmsApiErrorAsString(err));
            if (show_dialog) {
                context_->PostUITask([=, this]() {
                    QString msg = tcTr("id_op_error") + ":" + QString::number((int) err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
                    TcDialog dialog(tcTr("id_error"), msg);
                    dialog.exec();
                });
            }
            return false;
        }
    }

    bool GrUserManager::Logout() {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto uid = GetUserId();
        auto password = GetPassword();
        auto hash_password = MD5::Hex(password);
        auto r = px_cms::CmsUserApi::Logout(host, port, appkey, uid, hash_password);
        if (r.has_value()) {
            LOGI("Logout: {} {}", GetUsername(), GetUserId());
            Clear();
        }
        else {
            auto err = r.error();
            LOGE("Logout failed, err: {}, msg: {}", (int)err, px_cms::CmsApiErrorAsString(err));
            QString msg = tcTr("id_op_error") + ":" + QString::number((int)err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
        }
        return true;
    }

    bool GrUserManager::ModifyUsername(const std::string& username) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto uid = GetUserId();
        auto password = GetPassword();
        auto hash_password = MD5::Hex(password);
        std::map<std::string, std::string> values = {
            {px_cms::kUserName, username}
        };
        auto r = px_cms::CmsUserApi::Update(host, port, appkey, uid, hash_password, values);
        if (r.has_value()) {
            auto user = r.value();
            this->UpdateUsername(user->username_);
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_update_success"));
            return true;
        }
        else {
            auto err = r.error();
            QString msg = tcTr("id_op_error") + ":" + QString::number((int)err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
            return false;
        }
    }

    bool GrUserManager::ModifyPassword(const std::string& new_password) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto uid = GetUserId();
        auto password = GetPassword();
        auto hash_password = MD5::Hex(password);
        auto new_hash_password = MD5::Hex(new_password);
        auto r = px_cms::CmsUserApi::UpdatePassword(host, port, appkey, uid, hash_password, new_hash_password);
        if (r.has_value()) {
            auto user = r.value();
            this->UpdatePassword(new_password);
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_update_success"));
            return true;
        }
        else {
            auto err = r.error();
            QString msg = tcTr("id_op_error") + ":" + QString::number((int)err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
            return false;
        }
    }

    bool GrUserManager::UpdateAvatar(const std::string& avatar_path) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto uid = GetUserId();
        auto password = GetPassword();
        auto hash_password = MD5::Hex(password);
        auto r = px_cms::CmsUserApi::UpdateAvatar(host, port, appkey, uid, hash_password, avatar_path);
        if (r.has_value()) {
            auto user = r.value();
            UpdateAvatarPath(user->avatar_path_);
            context_->NotifyAppMessage(tcTr("id_tips"), tcTr("id_update_success"));
            return true;
        }
        else {
            auto err = r.error();
            QString msg = tcTr("id_op_error") + ":" + QString::number((int)err) + " " + px_cms::CmsApiErrorAsString(err).c_str();
            TcDialog dialog(tcTr("id_error"), msg);
            dialog.exec();
            return false;
        }
    }

    std::vector<std::shared_ptr<px_cms::CmsUserDevice>> GrUserManager::QueryBindDevices(int page, int page_size, bool show_dialog) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto uid = GetUserId();
        auto password = GetPassword();
        auto hash_password = MD5::Hex(password);
        if (uid.empty()) {
            return {};
        }
        auto r = px_cms::CmsUserDeviceApi::QueryUserBindDevices(host, port, appkey, uid, page, page_size);
        if (!r.has_value()) {
            auto err = r.error();
            if (show_dialog) {
                grApp->GetContext()->PostUITask([=, this]() {

                });
            }
            return {};
        }
        else {
            auto v = r.value();
            return v;
        }
    }

    std::shared_ptr<px_cms::CmsUserDevice> GrUserManager::AddDeviceForUser(const std::string& device_id) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto uid = GetUserId();
        if (uid.empty()) {
            return nullptr;
        }
        auto r = px_cms::CmsUserDeviceApi::AddDeviceForUser(host, port, appkey, uid, device_id);
        if (!r.has_value()) {
            auto ctx = grApp->GetContext();
            ctx->PostUITask([=, this]() {

            });
            return nullptr;
        }
        auto device = r.value();
        return device;
    }

    std::shared_ptr<px_cms::CmsUserDevice> GrUserManager::RemoveDeviceFromUser(const std::string& device_id) {
        auto host = settings_->GetCmsServerHost();
        auto port = settings_->GetCmsServerPort();
        auto appkey = grApp->GetAppkey();
        auto uid = GetUserId();
        if (uid.empty()) {
            return nullptr;
        }
        auto r = px_cms::CmsUserDeviceApi::RemoveDeviceFromUser(host, port, appkey, uid, device_id);
        if (!r.has_value()) {
            return nullptr;
        }
        auto device = r.value();
        return device;
    }

    void GrUserManager::SaveUserInfo(const std::string& uid, const std::string& username, const std::string& password, const std::string& avatar_path) {
        // uid
        context_->SpPutString(KeyUid(), uid);

        // username
        this->UpdateUsername(username);

        // password
        this->UpdatePassword(password);

        // avatar path
        this->UpdateAvatarPath(avatar_path);
    }

    bool GrUserManager::IsLoggedIn() {
        auto uid = GetUserId();
        auto username = GetUsername();
        auto password = GetPassword();
        return !uid.empty() && !username.empty() && !password.empty();
    }

    std::string GrUserManager::GetUserId() {
        return context_->SpGetString(KeyUid());
    }

    void GrUserManager::UpdateUsername(const std::string& username) {
        context_->SpPutString(KeyUsername(), username);
    }

    std::string GrUserManager::GetUsername() {
        return context_->SpGetString(KeyUsername());
    }

    void GrUserManager::UpdatePassword(const std::string& password) {
        context_->SpPutString(KeyPassword(), password);
    }

    std::string GrUserManager::GetPassword() {
        return context_->SpGetString(KeyPassword());
    }

    void GrUserManager::UpdateAvatarPath(const std::string& avatar_path) {
        context_->SpPutString(KeyAvatarPath(), avatar_path);
    }

    std::string GrUserManager::GetAvatarPath() {
        return context_->SpGetString(KeyAvatarPath());
    }

    void GrUserManager::Clear() {
        SaveUserInfo("", "", "", "");
    }

    std::string GrUserManager::KeyUid() {
        return std::format("{}{}", kUserPrefix, px_cms::kUserId);
    }

    std::string GrUserManager::KeyUsername() {
        return std::format("{}{}", kUserPrefix, px_cms::kUserName);
    }

    std::string GrUserManager::KeyPassword() {
        return std::format("{}{}", kUserPrefix, px_cms::kUserPassword);
    }

    std::string GrUserManager::KeyAvatarPath() {
        return std::format("{}{}", kUserPrefix, px_cms::kUserAvatarPath);
    }

}