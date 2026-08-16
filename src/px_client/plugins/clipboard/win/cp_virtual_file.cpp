#include "cp_virtual_file.h"

#define WIN32_LEAN_AND_MEAN
#include <wininet.h>
#include <Windows.h>
#include <iostream>
#include <string>
#include <memory>
#include <iostream>
#include <fstream>
#include <shlobj.h>
#include <shellapi.h>
#include <format>
#include <QFileInfo>
#include "cp_file_stream.h"
#include "px_common_new/log.h"
#include "ct_base_workspace.h"
#include "px_client/plugin_interface/ct_plugin_events.h"
#include "px_message_new/proto_converter.h"
#include "px_client/plugins/clipboard/clipboard_plugin.h"

#pragma comment(lib, "Wininet.lib")

namespace px
{

    CpVirtualFile::CpVirtualFile(ClientClipboardPlugin* plugin) {
        plugin_ = plugin;
        plugin_lifetime_token_ = plugin ? plugin->GetLifetimeToken() : nullptr;
        if (plugin) {
            event_cbk_ = [plugin, token = plugin_lifetime_token_](const std::shared_ptr<ClientPluginBaseEvent>& event) {
                if (!plugin || !token || !token->load()) {
                    return;
                }
                plugin->CallbackEvent(event);
            };
            request_buffer_cbk_ = [plugin, token = plugin_lifetime_token_](const ClipboardFileWrapper& file_wrapper,
                                                                            int64_t req_index,
                                                                            int64_t req_start,
                                                                            ULONG req_size) -> bool {
                if (!plugin || !token || !token->load()) {
                    return false;
                }
                auto settings = plugin->GetPluginSettings();
                px::Message msg;
                msg.set_device_id(settings.device_id_);
                msg.set_stream_id(settings.stream_id_);
                msg.set_type(MessageType::kClipboardReqBuffer);
                auto req_buffer = msg.mutable_cp_req_buffer();
                req_buffer->set_req_index(req_index);
                req_buffer->set_req_size(req_size);
                req_buffer->set_req_start(req_start);
                req_buffer->set_full_name(file_wrapper.file_.full_path());

                auto event = std::make_shared<ClientPluginNetworkEvent>();
                event->media_channel_ = false;
                event->buf_ = px::ProtoAsData(&msg);
                plugin->CallbackEvent(event);
                return true;
            };
        }
    }

    CpVirtualFile::~CpVirtualFile() {
        stream_owner_alive_->store(false);
        ExitAllStreams();
    }

    void CpVirtualFile::Init() {
        clip_format_filedesc_ = RegisterClipboardFormat(CFSTR_FILEDESCRIPTOR);
        clip_format_filecontent_ = RegisterClipboardFormat(CFSTR_FILECONTENTS);
        clip_format_preferred_ = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
        clip_format_hdrop_ = CF_HDROP;
    }

    STDMETHODIMP CpVirtualFile::GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) {
        ZeroMemory(pmedium, sizeof(*pmedium));

        HRESULT hr = DATA_E_FORMATETC;
        if (pformatetcIn->cfFormat == clip_format_filedesc_) {
            uint32_t file_count = menu_files_.size();
            if (file_count <= 0) {
                return S_FALSE;
            }
            if (pformatetcIn->tymed & TYMED_HGLOBAL) {
                UINT cb = sizeof(FILEGROUPDESCRIPTORW) + (file_count - 1) * sizeof(FILEDESCRIPTORW);
                HGLOBAL h = GlobalAlloc(GHND | GMEM_SHARE, cb);
                if (!h) {
                    hr = E_OUTOFMEMORY;
                    LOGE("GlobalAlloc failed!");
                    return hr;
                }

                auto group_descriptor = (FILEGROUPDESCRIPTORW*)::GlobalLock(h);
                if (!group_descriptor) {
                    LOGE("GlobalLock failed!");
                    GlobalFree(h);
                    return S_FALSE;
                }
                if (GlobalSize(h) < cb) {
                    LOGE("GlobalSize too small, expect: {}, actual: {}", cb, GlobalSize(h));
                    ::GlobalUnlock(h);
                    GlobalFree(h);
                    return STG_E_MEDIUMFULL;
                }

                group_descriptor->cItems = file_count;
                auto fd_array = (FILEDESCRIPTORW *) ((LPBYTE) group_descriptor +sizeof(UINT));

                for (uint32_t index = 0; index < file_count; ++index) {
                    auto clipboard_file = menu_files_.at(index);
                    auto target_filename = QString::fromStdString(clipboard_file.ref_path()).toStdWString();
                    wcsncpy_s(fd_array[index].cFileName,
                              _countof(fd_array[index].cFileName), target_filename.c_str(), _TRUNCATE);
                    fd_array[index].dwFlags =
                            FD_FILESIZE | FD_ATTRIBUTES | FD_CREATETIME | FD_WRITESTIME | FD_PROGRESSUI;
                    uint64_t file_size = static_cast<uint64_t>(clipboard_file.total_size());
                    fd_array[index].nFileSizeLow = static_cast<DWORD>(file_size & 0xFFFFFFFF);
                    fd_array[index].nFileSizeHigh = static_cast<DWORD>((file_size >> 32) & 0xFFFFFFFF);
                    fd_array[index].dwFileAttributes = FILE_ATTRIBUTE_NORMAL;

                    SYSTEMTIME lt;
                    GetLocalTime(&lt);
                    FILETIME ft;
                    SystemTimeToFileTime(&lt, &ft);
                    fd_array[index].ftLastAccessTime = ft;
                    fd_array[index].ftCreationTime = ft;
                    fd_array[index].ftLastWriteTime = ft;
                }

                ::GlobalUnlock(h);

                pmedium->hGlobal = h;
                pmedium->tymed = TYMED_HGLOBAL;
                hr = S_OK;
            }
        } else if (pformatetcIn->cfFormat == clip_format_filecontent_) {
            if ((pformatetcIn->tymed & TYMED_ISTREAM)) {
                auto file_index = pformatetcIn->lindex;
                if (file_index < 0 || task_files_.size() <= static_cast<size_t>(file_index)) {
                    LOGE("Invalid file index: {}, we only have: {}", file_index, menu_files_.size());
                    return S_FALSE;
                }
                menu_files_.clear();

                auto fw = task_files_.at(file_index);
                LOGI("Will get data stream for index: {}, name: {}", file_index, fw.file_.file_name());
                const auto full_path = fw.file_.full_path();
                RemoveStreamByPath(full_path);

                auto* file_stream = new CpFileStream(
                    request_buffer_cbk_,
                    plugin_lifetime_token_,
                    [this, full_path, token = stream_owner_alive_](CpFileStream* stream) {
                        if (!token || !token->load()) {
                            return;
                        }
                        std::lock_guard<std::mutex> lock(active_streams_mtx_);
                        auto it = active_streams_.find(full_path);
                        if (it != active_streams_.end() && it->second == stream) {
                            active_streams_.erase(it);
                        }
                    },
                    fw
                );
                {
                    std::lock_guard<std::mutex> lock(active_streams_mtx_);
                    active_streams_[full_path] = file_stream;
                }

                ReportFileTransferBegin(file_stream);

                pmedium->pstm = file_stream;
                pmedium->tymed = TYMED_ISTREAM;
                hr = S_OK;
            }
        } else if (pformatetcIn->cfFormat == clip_format_preferred_) {
            if (pformatetcIn->tymed & TYMED_HGLOBAL) {
                HGLOBAL h = GlobalAlloc(GHND | GMEM_SHARE, sizeof(DWORD));
                if (!h) {
                    LOGE("GlobalAlloc failed for preferred drop effect!");
                    return E_OUTOFMEMORY;
                }
                auto* pdw = (DWORD*)::GlobalLock(h);
                if (!pdw) {
                    LOGE("GlobalLock failed for preferred drop effect!");
                    GlobalFree(h);
                    return S_FALSE;
                }
                *pdw = DROPEFFECT_COPY;
                ::GlobalUnlock(h);
                pmedium->hGlobal = h;
                pmedium->tymed = TYMED_HGLOBAL;
                hr = S_OK;
            }
        } else if (pformatetcIn->cfFormat == clip_format_hdrop_) {
            if (pformatetcIn->tymed & TYMED_HGLOBAL) {
                uint32_t file_count = menu_files_.size();
                if (file_count <= 0) {
                    return S_FALSE;
                }
                std::vector<std::wstring> names;
                size_t total_chars = 1; // final null terminator
                for (const auto& f : menu_files_) {
                    std::wstring name = QString::fromStdString(f.ref_path()).toStdWString();
                    for (auto& ch : name) {
                        if (ch == L'/') ch = L'\\';
                    }
                    names.push_back(name);
                    total_chars += name.size() + 1;
                }
                const size_t bytes = sizeof(DROPFILES) + total_chars * sizeof(wchar_t);
                HGLOBAL h = GlobalAlloc(GHND | GMEM_SHARE, bytes);
                if (!h) {
                    return E_OUTOFMEMORY;
                }
                auto* df = (DROPFILES*)::GlobalLock(h);
                if (!df) {
                    GlobalFree(h);
                    return S_FALSE;
                }
                df->pFiles = sizeof(DROPFILES);
                df->fWide = TRUE;
                auto* dst = (wchar_t*)((LPBYTE)df + sizeof(DROPFILES));
                size_t remaining = total_chars;
                for (const auto& name : names) {
                    wcsncpy_s(dst, remaining, name.c_str(), _TRUNCATE);
                    const size_t len = name.size() + 1;
                    dst += len;
                    remaining -= len;
                }
                *dst = L'\0';
                ::GlobalUnlock(h);
                pmedium->hGlobal = h;
                pmedium->tymed = TYMED_HGLOBAL;
                hr = S_OK;
            }
        } else if (SUCCEEDED(_EnsureShellDataObject())) {
            hr = _pdtobjShell->GetData(pformatetcIn, pmedium);
        }
        return hr;
    }

    STDMETHODIMP CpVirtualFile::QueryGetData(FORMATETC *pformatetc) {
        HRESULT hr = S_FALSE;
        if (pformatetc->cfFormat == clip_format_filedesc_ ||
            pformatetc->cfFormat == clip_format_filecontent_ ||
            pformatetc->cfFormat == clip_format_preferred_ ||
            pformatetc->cfFormat == clip_format_hdrop_) {
            hr = S_OK;
        } else if (SUCCEEDED(_EnsureShellDataObject())) {
            hr = _pdtobjShell->QueryGetData(pformatetc);
        }
        return hr;
    }

    STDMETHODIMP CpVirtualFile::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) {
        *ppenumFormatEtc = NULL;
        HRESULT hr = E_NOTIMPL;
        if (dwDirection == DATADIR_GET) {
            FORMATETC rgfmtetc[] = {
                // the order here defines the accuarcy of rendering
                {(CLIPFORMAT) clip_format_filedesc_, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
                {(CLIPFORMAT) clip_format_filecontent_, NULL, DVASPECT_CONTENT, -1, TYMED_ISTREAM},
                {(CLIPFORMAT) clip_format_hdrop_, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
                {(CLIPFORMAT) clip_format_preferred_, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
            };
            hr = SHCreateStdEnumFmtEtc(ARRAYSIZE(rgfmtetc), rgfmtetc, ppenumFormatEtc);
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE CpVirtualFile::SetAsyncMode(BOOL fDoOpAsync) {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CpVirtualFile::GetAsyncMode(BOOL *pfIsOpAsync) {
        *pfIsOpAsync = true;// VARIANT_TRUE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CpVirtualFile::StartOperation(IBindCtx *pbcReserved) {
        in_async_op_ = true;
        IOperationsProgressDialog *pDlg = nullptr;
        ::CoCreateInstance(CLSID_ProgressDialog, NULL, CLSCTX_INPROC_SERVER, IID_IOperationsProgressDialog, (LPVOID *) &pDlg);
        LOGI("StartOperation....");
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CpVirtualFile::InOperation(BOOL *pfInAsyncOp) {
        *pfInAsyncOp = in_async_op_;
        LOGI("InOperation....");
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CpVirtualFile::EndOperation(HRESULT hResult, IBindCtx *pbcReserved, DWORD dwEffects) {
        in_async_op_ = false;
        LOGI("EndOperation....");
        ExitAllStreams();
        return S_OK;
    }

    void CpVirtualFile::OnClipboardFilesInfo(const std::vector<ClipboardFile>& files) {
        menu_files_ = files;
        task_files_.clear();
        for (const auto& file : files) {
            ClipboardFile cpy_file;
            cpy_file.CopyFrom(file);
            task_files_.push_back(ClipboardFileWrapper {
                .file_ = cpy_file,
            });
        }
        LOGI("On clipboard files size: {}", files.size());
        for (const auto& file : files) {
            LOGI("Name: {}, ref path: {}, total size: {}KB", file.file_name(), file.ref_path(), file.total_size()/1024);
        }
    }

    void CpVirtualFile::OnClipboardRespBuffer(const ClipboardRespBuffer& resp_buffer) {
        auto* stream = FindStreamByPath(resp_buffer.full_name());
        if (!stream) {
            LOGW("clipboard response has no matching stream: {}", resp_buffer.full_name());
            return;
        }
        stream->OnClipboardRespBuffer(resp_buffer);
        stream->Release();
    }

    void CpVirtualFile::ReportFileTransferBegin(CpFileStream* stream) {
        if (!stream || !event_cbk_ || !plugin_lifetime_token_ || !plugin_lifetime_token_->load()) {
            return;
        }

        const auto& settings = plugin_->GetPluginSettings();

//        auto event = std::make_shared<PxPluginFileTransferBegin>();
//        event->the_file_id_ = file_stream_->GetFileId();
//        event->begin_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
//        event->visitor_device_id_ = settings.device_id_;
//        event->direction_ = "In";
//        event->file_detail_ = file_stream_->GetFileName();
//        plugin_->CallbackEvent(event);

        auto event = std::make_shared<ClientPluginFileTransferBeginEvent>();
        event->task_id_ = stream->GetFileId();
        event->file_path_ = stream->GetFileName();
        event->direction_ = "In";
        event_cbk_(event);

        // send begin message to render
        px::Message msg;
        msg.set_device_id(settings.device_id_);
        msg.set_stream_id(settings.stream_id_);
        msg.set_type(MessageType::kClipboardReqAtBegin);
        auto req_buffer = msg.mutable_cp_req_at_begin();
        req_buffer->set_full_name(stream->GetFullPath());
        auto buffer= ProtoAsData(&msg);
        auto net_event = std::make_shared<ClientPluginNetworkEvent>();
        net_event->media_channel_ = true;
        net_event->buf_ = buffer;
        event_cbk_(net_event);
        //plugin_->DispatchTargetFileTransferMessage(file_stream_->GetStreamId(), buffer, false);
    }

    void CpVirtualFile::ReportFileTransferEnd(CpFileStream* stream) {
        if (!stream || !event_cbk_ || !plugin_lifetime_token_ || !plugin_lifetime_token_->load()) {
            return;
        }
//        auto event = std::make_shared<PxPluginFileTransferEnd>();
//        event->the_file_id_ = file_stream_->GetFileId();
//        event->end_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
//        event->success_ = true;
//        plugin_->CallbackEvent(event);

        const auto& settings = plugin_->GetPluginSettings();

        auto event = std::make_shared<ClientPluginFileTransferEndEvent>();
        event->task_id_ = stream->GetFileId();
        event->file_path_ = stream->GetFullPath();
        event->direction_ = "In";
        event->success_ = true;
        event_cbk_(event);

        // send end message to client
        px::Message msg;
        msg.set_device_id(settings.device_id_);
        msg.set_stream_id(settings.stream_id_);
        msg.set_type(MessageType::kClipboardReqAtEnd);
        auto req_buffer = msg.mutable_cp_req_at_end();
        req_buffer->set_full_name(stream->GetFullPath());
        req_buffer->set_success(true);
        auto buffer = ProtoAsData(&msg);
        auto net_event = std::make_shared<ClientPluginNetworkEvent>();
        net_event->media_channel_ = true;
        net_event->buf_ = buffer;
        event_cbk_(net_event);
        //plugin_->DispatchTargetFileTransferMessage(file_stream_->GetStreamId(), buffer, false);
    }

    void CpVirtualFile::ExitAllStreams() {
        std::vector<CpFileStream*> streams;
        {
            std::lock_guard<std::mutex> lock(active_streams_mtx_);
            for (const auto& [_, stream] : active_streams_) {
                if (stream) {
                    stream->AddRef();
                    streams.push_back(stream);
                }
            }
            active_streams_.clear();
        }
        for (auto* stream : streams) {
            ReportFileTransferEnd(stream);
            stream->Exit();
            stream->Release();
        }
    }

    void CpVirtualFile::RemoveStreamByPath(const std::string& full_path) {
        CpFileStream* stream = nullptr;
        {
            std::lock_guard<std::mutex> lock(active_streams_mtx_);
            auto it = active_streams_.find(full_path);
            if (it != active_streams_.end()) {
                stream = it->second;
                if (stream) {
                    stream->AddRef();
                }
                active_streams_.erase(it);
            }
        }
        if (!stream) {
            return;
        }
        ReportFileTransferEnd(stream);
        stream->Exit();
        stream->Release();
    }

    CpFileStream* CpVirtualFile::FindStreamByPath(const std::string& full_path) {
        std::lock_guard<std::mutex> lock(active_streams_mtx_);
        auto it = active_streams_.find(full_path);
        if (it == active_streams_.end() || !it->second) {
            return nullptr;
        }
        it->second->AddRef();
        return it->second;
    }

    CpVirtualFile* CreateVirtualFile(REFIID riid, void **ppv, ClientClipboardPlugin* plugin) {
        *ppv = nullptr;
        auto p = new CpVirtualFile(plugin);
        p->Init();
        auto hr = p->QueryInterface(riid, ppv);
        if (SUCCEEDED(hr)) {
            p->Release();
        }
        return p;
    }
};
