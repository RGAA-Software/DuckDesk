//
// Created by RGAA on 8/04/2025.
//

#include "cp_file_stream.h"
#include "px_common/log.h"
#include <span>

namespace px
{

    HRESULT STDMETHODCALLTYPE CpFileStream::QueryInterface(REFIID riid, void **ppvObject) {
        if (ppvObject == nullptr)
            return E_INVALIDARG;

        *ppvObject = nullptr;

        if (IsEqualIID(IID_IUnknown, riid) ||
            IsEqualIID(IID_ISequentialStream, riid) ||
            IsEqualIID(IID_IStream, riid)) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    // NOLINT(gammaray-raw-pointer-boundary): IStream ABI, neither destination nor byte count is retained.
    HRESULT STDMETHODCALLTYPE CpFileStream::Read(void* pv, ULONG cb, ULONG* pcbRead) {
        const auto destination = pv ? std::span(static_cast<char*>(pv), cb) : std::span<char>{};
        const auto count = pcbRead ? std::span(pcbRead, 1) : std::span<ULONG>{};
        if (!count.empty()) {
            count.front() = 0;
        }
        if (!pv && cb != 0) {
            return STG_E_INVALIDPOINTER;
        }
        std::unique_lock read_lock(read_mtx_);
        if (exit_ || !request_buffer_cb_ || !lifetime_token_ || !lifetime_token_->load()) {
            return S_FALSE;
        }
        if (cb == 0) {
            return S_OK;
        }
        const auto position = current_position_.load();
        const auto total = cp_file_.file_.total_size();
        if (position < 0 || total < 0 || position >= total) {
            return S_FALSE;
        }
        const auto size = std::min({static_cast<std::int64_t>(cb), kClipboardReadChunkBytes, total - position});
        std::optional<ClipboardReadRequest> request{};
        {
            std::lock_guard lock(wait_data_mtx_);
            resp_buffer_.reset();
            request = pending_read_.Begin(cp_file_.file_.full_path(), position, size);
        }
        if (!request) {
            return STG_E_READFAULT;
        }
        bool sent{};
        try {
            sent = request_buffer_cb_(cp_file_, request->index, request->offset, static_cast<ULONG>(request->size));
        } catch (...) {
            sent = false;
        }
        std::unique_lock lock(wait_data_mtx_);
        if (!sent) {
            pending_read_.Cancel();
            resp_buffer_.reset();
            return S_FALSE;
        }
        const auto exit_state = std::ref(exit_);
        const auto response_state = std::ref(resp_buffer_);
        data_cv_.wait_for(lock, std::chrono::seconds(10),
                          [exit_state, response_state] { return exit_state.get().load() || response_state.get().has_value(); });
        pending_read_.Cancel();
        if (exit_ || !lifetime_token_->load() || !resp_buffer_) {
            resp_buffer_.reset();
            return S_FALSE;
        }
        auto response = std::move(*resp_buffer_);
        resp_buffer_.reset();
        // The same shared validator already rejected mismatched or oversized responses at ingress.
        const auto bytes = std::span(response.buffer());
        std::ranges::copy(bytes, destination.begin());
        current_position_ += response.read_size();
        if (!count.empty()) {
            count.front() = static_cast<ULONG>(bytes.size());
        }
        return bytes.empty() ? S_FALSE : S_OK;
    }

    HRESULT STDMETHODCALLTYPE CpFileStream::Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* new_pos) {
        switch (dwOrigin) {
            case STREAM_SEEK_SET:
                current_position_ = 0;
                if (new_pos) {
                    new_pos->QuadPart = 0;
                }
                LOGI("seek set: {}", cp_file_.file_.file_name());
                break;
            case STREAM_SEEK_CUR:
                LOGI("seek current: {}", cp_file_.file_.file_name());
                break;
            case STREAM_SEEK_END:
                LOGI("seek end: {}", cp_file_.file_.file_name());
                break;
            default:
                return STG_E_INVALIDFUNCTION;
        }
        return S_OK;
    }

    // 没有被调用
    HRESULT WINAPI CpFileStream::Stat(STATSTG *pstatstg, DWORD grfStatFlag) {
        memset(pstatstg, 0, sizeof(STATSTG));
        pstatstg->pwcsName = NULL;
        pstatstg->type = STGTY_STREAM;
        pstatstg->cbSize.QuadPart = cp_file_.file_.total_size();
        return S_OK;
    }

    void CpFileStream::OnClipboardRespBuffer(const ClipboardRespBuffer& rb) {
        std::unique_lock lk(wait_data_mtx_);
        if (exit_ || !lifetime_token_ || !lifetime_token_->load() || resp_buffer_ || !pending_read_.Accepts(rb)) {
                return;
        }
        resp_buffer_ = rb;
        data_cv_.notify_all();
    }

    void CpFileStream::Exit() {
        {
                std::lock_guard lock(wait_data_mtx_);
                exit_ = true;
                pending_read_.Cancel();
                resp_buffer_.reset();
        }
        data_cv_.notify_all();
    }

    std::string CpFileStream::GetFileId() {
        return gen_file_id_;
    }

    std::string CpFileStream::GetFileName() {
        return cp_file_.file_.file_name();
    }

    std::string CpFileStream::GetFullPath() {
        return cp_file_.file_.full_path();
    }

    Microsoft::WRL::ComPtr<CpFileStream> CreateClipboardFileStream(
        CpFileStream::RequestBufferCallback request_buffer_callback,
        std::shared_ptr<std::atomic_bool> lifetime_token,
        const ClipboardFileWrapper& file_wrapper) {
        Microsoft::WRL::ComPtr<CpFileStream> stream;
        stream.Attach(new CpFileStream( // NOLINT(gammaray-raw-pointer-boundary): COM object is immediately adopted by ComPtr
            std::move(request_buffer_callback), std::move(lifetime_token),
            file_wrapper));
        return stream;
    }
}
