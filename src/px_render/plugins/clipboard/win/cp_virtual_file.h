// #pragma once

// #include <cstdint>
// #include <string>
// #include <vector>
// #include <map>
// #include <memory>
// #include <mutex>
// #include <atomic>
// #include <functional>
// #include <windows.h>
// #include <shlwapi.h>
// #include <strsafe.h>
// #include <shlobj.h>
// #include "cp_file_struct.h"
// #include "px_common_new/log.h"

// #pragma comment(lib, "shlwapi.lib")

// namespace px
// {

//     class CpFileStream;
//     class ClipboardPlugin;
//     class Data;
//     class GrPluginBaseEvent;

//     class CpVirtualFile : public IDataObject/*, public IDataObjectAsyncCapability*/ {
//     public:
//         explicit CpVirtualFile(ClipboardPlugin* plugin);
//         ~CpVirtualFile();

//         void Init();

//         HRESULT QueryInterface(REFIID riid, void **ppv) override {
// //            if (IsEqualIID(IID_IDataObjectAsyncCapability, riid)) {
// //                *ppv = (IDataObjectAsyncCapability *) this;
// //                CpDataObject::AddRef();
// //                LOGI("Query interface => IID_IDataObjectAsyncCapability");
// //                return S_OK;
// //            }
// //            return CpDataObject::QueryInterface(riid, ppv);

//             static const QITAB qit[] = {
//                 QITABENT(CpVirtualFile, IDataObject),
//                 { 0 },
//             };
//             return QISearch(this, qit, riid, ppv);
//         }

//         ULONG AddRef() override {
//             return InterlockedIncrement(&_cRef);
//         }

//         ULONG Release() override {
//             long cRef = InterlockedDecrement(&_cRef);
//             if (0 == cRef) {
//                 delete this;
//             }
//             return cRef;
//         }

//         long GetRefCount() {
//             return _cRef;
//         }

//         HRESULT GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) override;
//         HRESULT GetDataHere(FORMATETC * /* pformatetc */, STGMEDIUM * /* pmedium */) override {
//             return DATA_E_FORMATETC;;
//         }

//         HRESULT QueryGetData(FORMATETC *pformatetc) override;
//         HRESULT EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) override;
//         HRESULT GetCanonicalFormatEtc(FORMATETC *pformatetcIn, FORMATETC *pFormatetcOut) override {
//             pformatetcIn->ptd = NULL;
//             return E_NOTIMPL;
//         }

//         HRESULT SetData(FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease) override {
//             return E_NOTIMPL;
//         }

//         HRESULT DAdvise(FORMATETC * /* pformatetc */, DWORD /* advf */, IAdviseSink * /* pAdvSnk */, DWORD * /* pdwConnection */) override {
//             return E_NOTIMPL;
//         }

//         HRESULT DUnadvise(DWORD /* dwConnection */) override {
//             return E_NOTIMPL;
//         }

//         HRESULT EnumDAdvise(IEnumSTATDATA ** /* ppenumAdvise */) override {
//             return E_NOTIMPL;
//         }

//         // IDataObjectAsyncCapability
// //        ULONG AddRef() override {
// //            return CpDataObject::AddRef();
// //        }
// //
// //        ULONG Release() override {
// //            return CpDataObject::Release();
// //        }

// //        HRESULT SetAsyncMode(/* [in] */ BOOL fDoOpAsync) override;
// //        HRESULT GetAsyncMode(/* [out] */ __RPC__out BOOL *pfIsOpAsync) override;
// //        HRESULT StartOperation(/* [optional][unique][in] */ __RPC__in_opt IBindCtx *pbcReserved) override;
// //        HRESULT InOperation(/* [out] */ __RPC__out BOOL *pfInAsyncOp) override;
// //        HRESULT EndOperation(
// //                /* [in] */ HRESULT hResult,
// //                /* [unique][in] */ __RPC__in_opt IBindCtx *pbcReserved,
// //                /* [in] */ DWORD dwEffects) override;

//         void OnClipboardFilesInfo(const std::string& device_id, const std::string& stream_id, const std::vector<ClipboardFile>& files);
//         void OnClipboardRespBuffer(const ClipboardRespBuffer& resp_buffer);

//     private:
//         using EventCallback = std::function<void(const std::shared_ptr<GrPluginBaseEvent>&)>;
//         using FileTransferDispatchCallback = std::function<void(const std::string&, std::shared_ptr<Data>, bool)>;

//         void ReportFileTransferBegin(CpFileStream* stream);
//         void ReportFileTransferEnd(CpFileStream* stream);
//         void ExitAllStreams();
//         void RemoveStreamByPath(const std::string& full_path);
//         CpFileStream* FindStreamByPath(const std::string& full_path);

//     private:
//         CLIPFORMAT clip_format_file_desc_ = 0;
//         CLIPFORMAT clip_format_file_content_ = 0;
//         CLIPFORMAT m_cfHdrop = 0;
//         CLIPFORMAT m_cfPreferredDropEffect = 0;
//         BOOL in_async_op_ = false;
//         ClipboardPlugin* plugin_ = nullptr;
//         std::shared_ptr<std::atomic_bool> plugin_lifetime_token_ = nullptr;
//         std::shared_ptr<std::atomic_bool> stream_owner_alive_ = std::make_shared<std::atomic_bool>(true);
//         EventCallback event_cbk_ = nullptr;
//         FileTransferDispatchCallback file_transfer_cbk_ = nullptr;
//         mutable std::mutex active_streams_mtx_;
//         std::map<std::string, CpFileStream*> active_streams_;
//         // 这里分成2个，当点击粘贴后，清空menu_files，传输过程中再点击粘贴不让他再重复粘贴了
//         std::vector<ClipboardFile> menu_files_;
//         std::vector<ClipboardFileWrapper> task_files_;
//         long _cRef;
//     };

//     CpVirtualFile* CreateVirtualFile(REFIID riid, void **ppv, ClipboardPlugin* plugin);

// };

