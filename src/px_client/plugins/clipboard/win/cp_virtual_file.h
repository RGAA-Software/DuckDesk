#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <QString>
#include <QFile>
#include <QFileInfo>
#include "cp_data_object.h"
#include "cp_file_struct.h"

namespace px
{

    class CpFileStream;
    class ClientClipboardPlugin;
    class ClientPluginBaseEvent;

    class CpVirtualFile : public CpDataObject, public IDataObjectAsyncCapability {
    public:
        explicit CpVirtualFile(ClientClipboardPlugin* ws);
        ~CpVirtualFile() override;

        void Init();

        IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) {
            if (IsEqualIID(IID_IDataObjectAsyncCapability, riid)) {
                *ppv = (IDataObjectAsyncCapability *) this;
                AddRef();
                return S_OK;
            }
            return CpDataObject::QueryInterface(riid, ppv);
        }

        IFACEMETHODIMP_(ULONG) AddRef() {
            return CpDataObject::AddRef();
        }

        IFACEMETHODIMP_(ULONG) Release() {
            return CpDataObject::Release();
        }

        IFACEMETHODIMP GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium);

        IFACEMETHODIMP QueryGetData(FORMATETC *pformatetc);

        IFACEMETHODIMP EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc);

        // IDataObjectAsyncCapability
        virtual HRESULT SetAsyncMode(/* [in] */ BOOL fDoOpAsync);

        virtual HRESULT GetAsyncMode(/* [out] */ __RPC__out BOOL *pfIsOpAsync);

        virtual HRESULT StartOperation(/* [optional][unique][in] */ __RPC__in_opt IBindCtx *pbcReserved);

        virtual HRESULT InOperation(/* [out] */ __RPC__out BOOL *pfInAsyncOp);

        virtual HRESULT EndOperation(
                /* [in] */ HRESULT hResult,
                /* [unique][in] */ __RPC__in_opt IBindCtx *pbcReserved,
                /* [in] */ DWORD dwEffects);

        void OnClipboardFilesInfo(const std::vector<ClipboardFile>& files);
        void OnClipboardRespBuffer(const ClipboardRespBuffer& resp_buffer);

    private:
        using EventCallback = std::function<void(const std::shared_ptr<ClientPluginBaseEvent>&)>;

        void ReportFileTransferBegin(CpFileStream* stream);
        void ReportFileTransferEnd(CpFileStream* stream);
        void ExitAllStreams();
        void RemoveStreamByPath(const std::string& full_path);
        CpFileStream* FindStreamByPath(const std::string& full_path);

    private:
        uint32_t clip_format_filedesc_ = 0;
        uint32_t clip_format_filecontent_ = 0;
        uint32_t clip_format_preferred_ = 0;
        uint32_t clip_format_hdrop_ = CF_HDROP;
        BOOL in_async_op_ = false;
        ClientClipboardPlugin* plugin_ = nullptr;
        std::shared_ptr<std::atomic_bool> plugin_lifetime_token_ = nullptr;
        std::shared_ptr<std::atomic_bool> stream_owner_alive_ = std::make_shared<std::atomic_bool>(true);
        EventCallback event_cbk_ = nullptr;
        std::function<bool(const ClipboardFileWrapper&, int64_t, int64_t, ULONG)> request_buffer_cbk_ = nullptr;
        mutable std::mutex active_streams_mtx_;
        std::map<std::string, CpFileStream*> active_streams_;
        std::vector<ClipboardFile> menu_files_;
        std::vector<ClipboardFileWrapper> task_files_;
    };

    CpVirtualFile* CreateVirtualFile(REFIID riid, void **ppv, ClientClipboardPlugin* plugin);

};

