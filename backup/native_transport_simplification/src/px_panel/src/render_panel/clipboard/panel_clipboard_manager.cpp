//
// Created by RGAA on 16/08/2024.
//

#include "panel_clipboard_manager.h"
#include <objidl.h>
#include "px_common/log.h"
#include "px_common/time_util.h"
#include "px_message.pb.h"
#include "px_message/rp_proto_converter.h"
#include "render_panel/px_context.h"
#include "render_panel/px_application.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/system/win/win_panel_message_loop.h"
#include "render_panel/clipboard/win/panel_cp_virtual_file.h"
#include "px_common/clipboard/clipboard_platform.h"
#include "px_common/clipboard/clipboard_echo.h"
#include <QPointer>

namespace px
{
    namespace {
        class OleInitGuard {
        public:
            OleInitGuard() {
                initialized_ = SUCCEEDED(::OleInitialize(nullptr));
            }
            ~OleInitGuard() {
                if (initialized_) {
                    ::OleUninitialize();
                }
            }
            bool Ok() const { return initialized_; }
        private:
            bool initialized_ = false;
        };
    }

    ClipboardManager::ClipboardManager(const std::shared_ptr<PxContext>& ctx) : QObject(nullptr) {
        context_ = ctx;
        clipboard_platform_ = clipboard::CreatePlatform();
    }

//    static bool GetClipboardFiles(HWND hwnd, std::vector<std::wstring>& files) {
//        files.clear();
//
//        if (!OpenClipboard(hwnd)) {
//            std::cerr << "Failed to open clipboard" << std::endl;
//            return false;
//        }
//
//        // 检查是否有文件被复制
//        HDROP hDrop = (HDROP)GetClipboardData(CF_HDROP);
//        if (hDrop) {
//            UINT fileCount = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
//
//            for (UINT i = 0; i < fileCount; i++) {
//                wchar_t filePath[MAX_PATH];
//                if (DragQueryFile(hDrop, i, filePath, MAX_PATH)) {
//                    files.push_back(filePath);
//                }
//            }
//        }
//
//        CloseClipboard();
//        return true;
//    }

//    void ClipboardManager::OnLocalClipboardUpdated(const std::shared_ptr<MsgClipboardEvent>& msg) {
//        LOGI("**clipboard update, type : {}, msg: {}, file size: {}", (int)msg->clipboard_type_, msg->text_msg_, msg->files_.size());
//        for (const auto& file : msg->files_) {
//            LOGI("** file name: {}, size: {}, full path: {}, ref path: {}", file.file_name_, file.total_size_, file.full_path_, file.ref_path_);
//        }
//
//        if (msg->clipboard_type_ == MsgClipboardType::kText) {
//            // send it to remote
//            px::Message m;
//            m.set_type(px::kClipboardInfo);
//            auto sub = m.mutable_clipboard_info();
//            sub->set_type(ClipboardType::kClipboardText);
//            sub->set_msg(msg->text_msg_);
//            auto buffer = ProtoAsData(&m);
//            // todo::
//            //plugin_->DispatchAllStreamMessage(buffer);
//        }
//        else if (msg->clipboard_type_ == MsgClipboardType::kFiles && !msg->files_.empty()) {
//            px::Message m;
//            m.set_type(px::kClipboardInfo);
//            auto sub = m.mutable_clipboard_info();
//            sub->set_type(ClipboardType::kClipboardFiles);
//            for (const auto& file : msg->files_) {
//                auto pf = sub->mutable_files()->Add();
//                pf->set_file_name(file.file_name_);
//                pf->set_full_path(file.full_path_);
//                pf->set_ref_path(file.ref_path_);
//                pf->set_total_size(file.total_size_);
//            }
//            auto buffer = ProtoAsData(&m);
//            // todo::
//            //plugin_->DispatchAllStreamMessage(buffer);
//        }
//    }

    void ClipboardManager::OnRemoteClipboardInfo(std::shared_ptr<Message> msg) {
        // USER_PROXY_MIGRATION: clipboard path disabled, see px_user_proxy
        (void)msg;
#if 0
        QPointer<ClipboardManager> self(this);
        if (msg->type() == MessageType::kClipboardInfo) {
            auto sub = msg->clipboard_info();
            LOGI("Remote Clipboard info, type : {}", (int)sub.type());
            if (sub.type() == ClipboardType::kClipboardText) {
                const auto in_text = sub.msg();
                if (in_text.empty()) {
                    return;
                }

                context_->PostTask([=]() {
                    if (!self || !self->clipboard_platform_) {
                        return;
                    }

                    std::shared_ptr<WinMessageLoop> msg_loop;
                    if (auto app = self->context_->GetApplication()) {
                        msg_loop = app->GetWinMessageLoop();
                    }
                    if (!msg_loop) {
                        LOGE("WinMessageLoop unavailable for remote clipboard apply");
                        return;
                    }

                    clipboard::SuppressOutboundGuard suppress_guard(msg_loop->GetEchoFilter());
                    msg_loop->SetRemoteClipboardEcho(in_text);

                    if (!clipboard::WriteTextWithRetry(*self->clipboard_platform_, in_text)) {
                        LOGE("Failed to apply remote clipboard text after retries");
                        return;
                    }

                    LOGI("*** update remote clipboard info: {}", in_text);

                    self->context_->SendAppMessage(MsgRemoteClipboardResp{
                        .text_msg_ = in_text,
                    });

                    px::Message resp_msg;
                    resp_msg.set_type(px::kClipboardInfoResp);
                    auto resp_sub = resp_msg.mutable_clipboard_info_resp();
                    resp_sub->set_type(ClipboardType::kClipboardText);
                    resp_sub->set_msg(in_text);
                    auto rp_msg = px::MakeRpRawRenderMessage(msg->stream_id(), msg->device_id(), resp_msg.SerializeAsString(), true);
                    self->context_->GetApplication()->PostMessage2Renderer(rp_msg);
                });
                
            }/*
            else if (sub.type() == ClipboardType::kClipboardImage) {
                auto in_image = sub.msg();
                QImage image;
                image.loadFromData((uchar*)in_image.c_str(), in_image.size(), "PNG");
                if (image.isNull()) {
                    LOGE("An invalid image...");
                    return;
                }
                LOGI("In image size: {}, {}x{}", in_image.size(), image.width(), image.height());
                for (int i = 0; i < 100; i++) {
                    QClipboard *board = QGuiApplication::clipboard();
                    board->setImage(image);
                    if (board->ownsClipboard()) {
                        LOGI("set image Success: {}", i);
                        break;
                    }
                    LOGI("Will try next: {}", i);
                    TimeUtil::DelayBySleep(5);
                }
            }*/

            if (sub.type() == ClipboardType::kClipboardFiles) {
                const auto& files = msg->clipboard_info().files();
                std::vector<ClipboardFile> target_files;
                for (auto& file : files) {
                    ClipboardFile cpy_file;
                    cpy_file.CopyFrom(file);
                    target_files.push_back(file);
                    LOGI("Clipboard file: {}", file.file_name());
                }

                context_->PostTask([=]() {
                    if (!self) {
                        return;
                    }
                    if (!self->virtual_file_) {
                        self->virtual_file_ = px::CreateVirtualFile(IID_IDataObject, (void **) &self->data_object_, self->context_);
                    }
                    if (!self->data_object_) {
                        LOGE("DataObject is null!");
                        return;
                    }

                    OleInitGuard ole_guard;
                    if (!ole_guard.Ok()) {
                        LOGE("OleInitialize failed!");
                        return;
                    }

                    bool cleared_clipboard = false;
                    for (int i = 0; i < 20; i++) {
                        if (self->clipboard_platform_ && self->clipboard_platform_->Clear()) {
                            cleared_clipboard = true;
                            break;
                        }
                        TimeUtil::DelayBySleep(10);
                    }
                    if (!cleared_clipboard) {
                        LOGE("Empty clipboard failed!");
                        return;
                    }

                    TimeUtil::DelayBySleep(10);

                    bool set_clipboard = false;
                    for (int i = 0; i < 20; i++) {
                        auto hr = ::OleSetClipboard(self->data_object_);
                        if (hr == S_OK) {
                            set_clipboard = true;
                            break;
                        }
                        TimeUtil::DelayBySleep(10);
                    }
                    if (!set_clipboard) {
                        LOGE("Set clipboard failed!");
                        return;
                    }

                    auto device_id = msg->device_id();
                    auto stream_id = msg->stream_id();
                    self->virtual_file_->OnClipboardFilesInfo(device_id, stream_id, target_files);
                });
            }
        }
#endif
        if (msg->type() == MessageType::kClipboardRespBuffer) {
            if (virtual_file_) {
                virtual_file_->OnClipboardRespBuffer(msg->cp_resp_buffer());
            }
        }
    }

}
