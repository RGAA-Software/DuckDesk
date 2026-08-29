//
// Created by RGAA on 8/04/2025.
//

#ifndef PX_CLIENT_CP_FILE_STREAM_H
#define PX_CLIENT_CP_FILE_STREAM_H

#include <cstdint>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <wrl/client.h>
#include "cp_data_object.h"
#include "cp_file_struct.h"
#include "px_message.pb.h"
#include "px_common_new/md5.h"

namespace px
{
    class CpFileStream : public IStream {
    public:
        using RequestBufferCallback = std::function<bool(const ClipboardFileWrapper&, int64_t, int64_t, ULONG)>;

        CpFileStream(RequestBufferCallback request_buffer_cb,
                     std::shared_ptr<std::atomic_bool> lifetime_token,
                     const ClipboardFileWrapper& fw) : ref_(1) {
            request_buffer_cb_ = std::move(request_buffer_cb);
            lifetime_token_ = std::move(lifetime_token);
            cp_file_ = fw;
            gen_file_id_ = MD5::Hex(cp_file_.file_.file_name());
        }

        virtual ~CpFileStream() {

        }

        HRESULT QueryInterface(REFIID riid, void **ppvObject) override;

        ULONG AddRef() override {
            return InterlockedIncrement(&ref_);
        }

        ULONG Release() override {
            ULONG newRef = InterlockedDecrement(&ref_);
            if (newRef == 0) {
                delete this;
            }
            return newRef;
        }

        HRESULT SetSize(ULARGE_INTEGER) override {
            return E_NOTIMPL;
        }

        HRESULT CopyTo(IStream *, ULARGE_INTEGER, ULARGE_INTEGER *, ULARGE_INTEGER *) override {
            return E_NOTIMPL;
        }

        HRESULT Commit(DWORD) override {
            return E_NOTIMPL;
        }

        HRESULT Revert(void) override {
            return E_NOTIMPL;
        }

        HRESULT LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
            return E_NOTIMPL;
        }

        HRESULT UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
            return E_NOTIMPL;
        }

        HRESULT Clone(IStream **) override {
            return E_NOTIMPL;
        }

        HRESULT Read(void *pv, ULONG cb, ULONG *pcbRead) override;

        HRESULT Write(const void *pv, ULONG cb, ULONG *pcbWritten) override {
            return S_OK;
        }

        HRESULT Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* new_pos) override;

        HRESULT WINAPI Stat(STATSTG *pstatstg, DWORD grfStatFlag) override;

        void OnClipboardRespBuffer(const ClipboardRespBuffer& rb);
        void Exit();
        std::string GetFileId();
        std::string GetFileName();
        std::string GetFullPath();

    private:
        LONG ref_;
        uint64_t file_size_ {0};
        std::atomic_int64_t current_position_ = 0;
        std::atomic_int64_t req_index_ = 0;
        ClipboardFileWrapper cp_file_;
        std::shared_ptr<std::atomic_bool> lifetime_token_ = nullptr;
        RequestBufferCallback request_buffer_cb_ = nullptr;

        std::atomic_bool exit_ = false;
        std::mutex read_mtx_;
        std::mutex wait_data_mtx_;
        std::condition_variable data_cv_;

        std::optional<ClipboardRespBuffer> resp_buffer_;
        std::string gen_file_id_;
    };

    Microsoft::WRL::ComPtr<CpFileStream> CreateClipboardFileStream(
        CpFileStream::RequestBufferCallback request_buffer_callback,
        std::shared_ptr<std::atomic_bool> lifetime_token,
        const ClipboardFileWrapper& file_wrapper);


}

#endif //PX_CLIENT_CP_FILE_STREAM_H
