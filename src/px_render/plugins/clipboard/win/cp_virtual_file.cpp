// #include "cp_virtual_file.h"
// #include <wininet.h>
// #include <windows.h>
// #include <iostream>
// #include <string>
// #include <memory>
// #include <iostream>
// #include <fstream>
// #include <shlobj.h>
// #include <format>
// #include "cp_file_stream.h"
// #include "px_common_new/log.h"
// #include "px_common_new/md5.h"
// #include "px_common_new/time_util.h"
// #include "px_common_new/string_util.h"
// #include "px_message_new/proto_converter.h"
// #include "px_render/plugin_interface/px_plugin_events.h"
// #include "px_render/plugins/clipboard/clipboard_plugin.h"

// #pragma comment(lib, "Wininet.lib")

// namespace tc
// {

//     CpVirtualFile::CpVirtualFile(ClipboardPlugin* plugin) {
//         _cRef = 1;
//         plugin_ = plugin;
//         plugin_lifetime_token_ = plugin ? plugin->GetLifetimeToken() : nullptr;
//         if (plugin) {
//             event_cbk_ = [plugin, token = plugin_lifetime_token_](const std::shared_ptr<GrPluginBaseEvent>& event) {
//                 if (!plugin || !token || !token->load()) {
//                     return;
//                 }
//                 plugin->CallbackEvent(event);
//             };
//             file_transfer_cbk_ = [plugin, token = plugin_lifetime_token_](const std::string& stream_id,
//                                                                           const std::shared_ptr<Data>& msg,
//                                                                           bool run_through) {
//                 if (!plugin || !token || !token->load()) {
//                     return;
//                 }
//                 plugin->DispatchTargetFileTransferMessage(stream_id, msg, run_through);
//             };
//         }
//     }

//     CpVirtualFile::~CpVirtualFile() {
//         stream_owner_alive_->store(false);
//         ExitAllStreams();
//     }

//     void CpVirtualFile::Init() {
//         clip_format_file_desc_ = RegisterClipboardFormat(CFSTR_FILEDESCRIPTOR);
//         clip_format_file_content_ = RegisterClipboardFormat(CFSTR_FILECONTENTS);
//         m_cfHdrop = CF_HDROP;
//         m_cfPreferredDropEffect = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
//         LOGI("CpVirtualFile, register, file desc: {}, file content: {}", clip_format_file_desc_, clip_format_file_content_);
//     }

//     HRESULT CpVirtualFile::GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) {
//         ZeroMemory(pmedium, sizeof(*pmedium));

//         //LOGI("====> GetData, format: {}", pformatetcIn->cfFormat);

//         HRESULT hr = DATA_E_FORMATETC;
//         if (pformatetcIn->cfFormat == clip_format_file_desc_) {
//             uint32_t file_count = menu_files_.size();
//             LOGI("GetData file format, file count: {}", file_count);
//             if (file_count <= 0) {
//                 return S_FALSE;
//             }
//             if (pformatetcIn->tymed & TYMED_HGLOBAL) {
//                 UINT cb = sizeof(FILEGROUPDESCRIPTORW) + (file_count - 1) * sizeof(FILEDESCRIPTORW);
//                 HGLOBAL h = GlobalAlloc(GHND | GMEM_SHARE, cb);
//                 if (!h) {
//                     hr = E_OUTOFMEMORY;
//                     LOGE("GlobalAlloc failed!");
//                     return hr;
//                 }

//                 auto group_descriptor = (FILEGROUPDESCRIPTORW*)::GlobalLock(h);
//                 if (!group_descriptor) {
//                     LOGE("GlobalLock failed!");
//                     GlobalFree(h);
//                     return S_FALSE;
//                 }
//                 if (GlobalSize(h) < cb) {
//                     LOGE("GlobalSize too small, expect: {}, actual: {}", cb, GlobalSize(h));
//                     ::GlobalUnlock(h);
//                     GlobalFree(h);
//                     return STG_E_MEDIUMFULL;
//                 }

//                 group_descriptor->cItems = file_count;
//                 auto fd_array = (FILEDESCRIPTORW *) ((LPBYTE) group_descriptor +sizeof(UINT));

//                 for (uint32_t index = 0; index < file_count; ++index) {
//                     auto clipboard_file = menu_files_.at(index);
//                     // use ref_path to process folder
//                     auto target_filename = StringUtil::ToWString(clipboard_file.ref_path());
//                     LOGI("GetData, file: {}, name: {}", clipboard_file.file_name(), StringUtil::ToUTF8(target_filename));
//                     wcsncpy_s(fd_array[index].cFileName, _countof(fd_array[index].cFileName), target_filename.c_str(), _TRUNCATE);

//                     fd_array[index].dwFlags = FD_FILESIZE | FD_ATTRIBUTES | FD_CREATETIME | FD_WRITESTIME | FD_PROGRESSUI;
//                     uint64_t file_size = static_cast<uint64_t>(clipboard_file.total_size());
//                     fd_array[index].nFileSizeLow = static_cast<DWORD>(file_size & 0xFFFFFFFF);
//                     fd_array[index].nFileSizeHigh = static_cast<DWORD>((file_size >> 32) & 0xFFFFFFFF);
//                     fd_array[index].dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
//                     GetSystemTimeAsFileTime(&fd_array[index].ftLastWriteTime);
//                     LOGI("GetData, total size: {}, high: {}, low: {}", file_size, fd_array[index].nFileSizeHigh, fd_array[index].nFileSizeLow);

//                     SYSTEMTIME lt;
//                     GetLocalTime(&lt);
//                     FILETIME ft;
//                     SystemTimeToFileTime(&lt, &ft);
//                     fd_array[index].ftLastAccessTime = ft;
//                     fd_array[index].ftCreationTime = ft;
//                     fd_array[index].ftLastWriteTime = ft;
//                 }

//                 ::GlobalUnlock(h);

//                 pmedium->hGlobal = h;
//                 pmedium->tymed = TYMED_HGLOBAL;
//                 hr = S_OK;
//             }
//         } else if (pformatetcIn->cfFormat == clip_format_file_content_) {
//             if ((pformatetcIn->tymed & TYMED_ISTREAM)) {
//                 auto file_index = pformatetcIn->lindex;
//                 if (file_index < 0 || task_files_.size() <= static_cast<size_t>(file_index)) {
//                     LOGE("Invalid file index: {}, we only have: {}", file_index, menu_files_.size());
//                     return S_FALSE;
//                 }
//                 menu_files_.clear();

//                 auto fw = task_files_.at(file_index);
//                 LOGI("Will get data stream for index: {}, name: {}", file_index, fw.file_.file_name());
//                 const auto full_path = fw.file_.full_path();
//                 RemoveStreamByPath(full_path);
//                 auto dispatch_cbk = file_transfer_cbk_;
//                 auto plugin_token = plugin_lifetime_token_;

//                 auto* file_stream = new CpFileStream(
//                     [dispatch_cbk, plugin_token](const ClipboardFileWrapper& file_wrapper, int64_t req_index, int64_t req_start, ULONG req_size) -> bool {
//                         if (!dispatch_cbk || !plugin_token || !plugin_token->load()) {
//                             return false;
//                         }
//                         tc::Message msg;
//                         msg.set_type(MessageType::kClipboardReqBuffer);
//                         auto req_buffer = msg.mutable_cp_req_buffer();
//                         req_buffer->set_req_index(req_index);
//                         req_buffer->set_req_size(req_size);
//                         req_buffer->set_req_start(req_start);
//                         req_buffer->set_full_name(file_wrapper.file_.full_path());
//                         dispatch_cbk(file_wrapper.stream_id_, ProtoAsData(&msg), false);
//                         return true;
//                     },
//                     plugin_lifetime_token_,
//                     [this, full_path, token = stream_owner_alive_](CpFileStream* stream) {
//                         if (!token || !token->load()) {
//                             return;
//                         }
//                         std::lock_guard<std::mutex> lock(active_streams_mtx_);
//                         auto it = active_streams_.find(full_path);
//                         if (it != active_streams_.end() && it->second == stream) {
//                             active_streams_.erase(it);
//                         }
//                     },
//                     fw
//                 );
//                 {
//                     std::lock_guard<std::mutex> lock(active_streams_mtx_);
//                     active_streams_[full_path] = file_stream;
//                 }

//                 ReportFileTransferBegin(file_stream);

//                 pmedium->pstm = file_stream;
//                 pmedium->tymed = TYMED_ISTREAM;
//                 hr = S_OK;
//             }
//         } /*else if (SUCCEEDED(EnsureShellDataObject())) {
//             hr = _pdtobjShell->GetData(pformatetcIn, pmedium);
//         }*/
//         return hr;
//     }

//     HRESULT CpVirtualFile::QueryGetData(FORMATETC *pformatetc) {
//         LOGI("CpVirtualFile, QueryGetData, format: {}", pformatetc->cfFormat);
//         HRESULT hr = S_FALSE;
//         if (pformatetc->cfFormat == clip_format_file_desc_ ||
//             pformatetc->cfFormat == clip_format_file_content_ ||
//             pformatetc->cfFormat == m_cfHdrop ||
//             pformatetc->cfFormat == m_cfPreferredDropEffect) {
//             hr = S_OK;
//             return S_OK;
//         }/* else if (SUCCEEDED(_EnsureShellDataObject())) {
//             hr = _pdtobjShell->QueryGetData(pformatetc);
//         }*/
//         //return hr;
//         return E_NOTIMPL;
//     }

//     HRESULT CpVirtualFile::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) {
//         *ppenumFormatEtc = NULL;
//         HRESULT hr = E_NOTIMPL;
//         if (dwDirection == DATADIR_GET) {
//             LOGI("Set format ...");
//             FORMATETC rgfmtetc[] = {
//                 // the order here defines the accuarcy of rendering
//                 {(CLIPFORMAT) clip_format_file_desc_, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
//                 {(CLIPFORMAT) clip_format_file_content_, NULL, DVASPECT_CONTENT, -1, TYMED_ISTREAM},
//                 { (CLIPFORMAT)m_cfHdrop, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
//                 { (CLIPFORMAT)m_cfPreferredDropEffect, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL }
//             };
//             hr = SHCreateStdEnumFmtEtc(ARRAYSIZE(rgfmtetc), rgfmtetc, ppenumFormatEtc);
//             if (hr != S_OK) {
//                 LOGE("SHCreateStdEnumFmtEtc failed!");
//             }
//         }
//         return hr;
//     }

// //    HRESULT STDMETHODCALLTYPE CpVirtualFile::SetAsyncMode(BOOL fDoOpAsync) {
// //        return S_OK;
// //    }
// //
// //    HRESULT STDMETHODCALLTYPE CpVirtualFile::GetAsyncMode(BOOL *pfIsOpAsync) {
// //        *pfIsOpAsync = true;// VARIANT_TRUE;
// //        return S_OK;
// //    }

// //    HRESULT STDMETHODCALLTYPE CpVirtualFile::StartOperation(IBindCtx *pbcReserved) {
// //        in_async_op_ = true;
// //        IOperationsProgressDialog *pDlg = nullptr;
// //        ::CoCreateInstance(CLSID_ProgressDialog, NULL, CLSCTX_INPROC_SERVER, IID_IOperationsProgressDialog, (LPVOID *) &pDlg);
// //        LOGI("StartOperation....");
// //        return S_OK;
// //    }
// //
// //    HRESULT STDMETHODCALLTYPE CpVirtualFile::InOperation(BOOL *pfInAsyncOp) {
// //        *pfInAsyncOp = in_async_op_;
// //        LOGI("InOperation....");
// //        return S_OK;
// //    }

// //    HRESULT STDMETHODCALLTYPE CpVirtualFile::EndOperation(HRESULT hResult, IBindCtx *pbcReserved, DWORD dwEffects) {
// //        in_async_op_ = false;
// //        LOGI("EndOperation....");
// //        if (file_stream_) {
// //            // report
// //            this->ReportFileTransferEnd();
// //
// //            file_stream_->Exit();
// //            file_stream_.reset();
// //        }
// //
// //        ::OleFlushClipboard();
// //        ::OleSetClipboard(nullptr);
// //        menu_files_.clear();
// //        task_files_.clear();
// //        return S_OK;
// //    }

//     void CpVirtualFile::OnClipboardFilesInfo(const std::string& device_id, const std::string& stream_id, const std::vector<ClipboardFile>& files) {
//         menu_files_ = files;
//         task_files_.clear();
//         for (const auto& file : files) {
//             ClipboardFile cpy_file;
//             cpy_file.CopyFrom(file);
//             task_files_.push_back(ClipboardFileWrapper {
//                 .device_id_ = device_id,
//                 .stream_id_ = stream_id,
//                 .file_ = cpy_file,
//             });
//         }
//         LOGI("On clipboard files size: {}", files.size());
//         for (const auto& file : files) {
//             LOGI("Name: {}, ref path: {}, total size: {}KB", file.file_name(), file.ref_path(), file.total_size()/1024);
//         }
//     }

//     void CpVirtualFile::OnClipboardRespBuffer(const ClipboardRespBuffer& resp_buffer) {
//         auto* stream = FindStreamByPath(resp_buffer.full_name());
//         if (!stream) {
//             LOGW("clipboard response has no matching stream: {}", resp_buffer.full_name());
//             return;
//         }
//         stream->OnClipboardRespBuffer(resp_buffer);
//         stream->Release();
//     }

//     void CpVirtualFile::ReportFileTransferBegin(CpFileStream* stream) {
//         if (!stream || !event_cbk_ || !plugin_lifetime_token_ || !plugin_lifetime_token_->load()) {
//             return;
//         }
//         auto event = std::make_shared<GrPluginFileTransferBegin>();
//         event->the_file_id_ = stream->GetFileId();
//         event->begin_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
//         event->visitor_device_id_ = stream->GetDeviceId();
//         event->direction_ = "In";
//         event->file_detail_ = stream->GetFileName();
//         event_cbk_(event);

//         // send begin message to client
//         // send end message to client
//         tc::Message msg;
//         msg.set_device_id(stream->GetDeviceId());
//         msg.set_stream_id(stream->GetStreamId());
//         msg.set_type(MessageType::kClipboardReqAtBegin);
//         auto req_buffer = msg.mutable_cp_req_at_begin();
//         req_buffer->set_full_name(stream->GetFullPath());
//         auto buffer = ProtoAsData(&msg);
//         file_transfer_cbk_(stream->GetStreamId(), buffer, false);
//     }

//     void CpVirtualFile::ReportFileTransferEnd(CpFileStream* stream) {
//         if (!stream || !event_cbk_ || !plugin_lifetime_token_ || !plugin_lifetime_token_->load()) {
//             return;
//         }
//         auto event = std::make_shared<GrPluginFileTransferEnd>();
//         event->the_file_id_ = stream->GetFileId();
//         event->end_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
//         event->success_ = true;
//         event_cbk_(event);

//         // send end message to client
//         tc::Message msg;
//         msg.set_device_id(stream->GetDeviceId());
//         msg.set_stream_id(stream->GetStreamId());
//         msg.set_type(MessageType::kClipboardReqAtEnd);
//         auto req_buffer = msg.mutable_cp_req_at_end();
//         req_buffer->set_full_name(stream->GetFullPath());
//         req_buffer->set_success(true);
//         auto buffer = ProtoAsData(&msg);
//         file_transfer_cbk_(stream->GetStreamId(), buffer, false);
//     }

//     void CpVirtualFile::ExitAllStreams() {
//         std::vector<CpFileStream*> streams;
//         {
//             std::lock_guard<std::mutex> lock(active_streams_mtx_);
//             for (const auto& [_, stream] : active_streams_) {
//                 if (stream) {
//                     stream->AddRef();
//                     streams.push_back(stream);
//                 }
//             }
//             active_streams_.clear();
//         }
//         for (auto* stream : streams) {
//             ReportFileTransferEnd(stream);
//             stream->Exit();
//             stream->Release();
//         }
//     }

//     void CpVirtualFile::RemoveStreamByPath(const std::string& full_path) {
//         CpFileStream* stream = nullptr;
//         {
//             std::lock_guard<std::mutex> lock(active_streams_mtx_);
//             auto it = active_streams_.find(full_path);
//             if (it != active_streams_.end()) {
//                 stream = it->second;
//                 if (stream) {
//                     stream->AddRef();
//                 }
//                 active_streams_.erase(it);
//             }
//         }
//         if (!stream) {
//             return;
//         }
//         ReportFileTransferEnd(stream);
//         stream->Exit();
//         stream->Release();
//     }

//     CpFileStream* CpVirtualFile::FindStreamByPath(const std::string& full_path) {
//         std::lock_guard<std::mutex> lock(active_streams_mtx_);
//         auto it = active_streams_.find(full_path);
//         if (it == active_streams_.end() || !it->second) {
//             return nullptr;
//         }
//         it->second->AddRef();
//         return it->second;
//     }

//     CpVirtualFile* CreateVirtualFile(REFIID riid, void **ppv, ClipboardPlugin* plugin) {
//         *ppv = nullptr;
//         auto p = new CpVirtualFile(plugin);
//         p->Init();
//         auto hr = p->QueryInterface(riid, ppv);
//         if (SUCCEEDED(hr)) {
//             p->Release();
//             LOGI("CpVirtualFile ref count: {}", p->GetRefCount());
//         }
//         else {
//             LOGE("Query Clipboard DataObject failed: {:x}", hr);
//         }
//         return p;
//     }
// };
