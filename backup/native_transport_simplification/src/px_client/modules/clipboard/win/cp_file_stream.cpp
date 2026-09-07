//
// Created by RGAA on 8/04/2025.
//

#include "cp_file_stream.h"
#include "px_common/log.h"

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

    HRESULT STDMETHODCALLTYPE CpFileStream::Read(void *pv, ULONG cb, ULONG *pcbRead) {
        std::unique_lock read_lock(read_mtx_);
        if (!pv) {
            return STG_E_INVALIDPOINTER;
        }
        if (pcbRead) {
            *pcbRead = 0;
        }
        if (exit_ || !request_buffer_cb_ || !lifetime_token_ || !lifetime_token_->load()) {
            return S_FALSE;
        }
        // Cap each request well below the file-transfer channel's max message size
        // (~256 KiB): a 256 KiB payload plus the protobuf/TLV header exceeds it and
        // the message is dropped, truncating the pasted file. Match the Rust side's
        // MAX_READ_CHUNK_SIZE (128 KiB); the shell re-issues IStream::Read for the
        // remaining bytes.
        ULONG req_size = cb;
        if (req_size > 128u * 1024u) {
            req_size = 128u * 1024u;
        }
        if (!request_buffer_cb_(cp_file_, req_index_.load(), current_position_.load(), req_size)) {
            return S_FALSE;
        }

        std::unique_lock lk(wait_data_mtx_);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        const auto exit_state = std::ref(exit_);
        const auto response_state = std::ref(resp_buffer_);
        while (true) {
            data_cv_.wait_until(lk, deadline, [exit_state, response_state]() {
                return exit_state.get().load() ||
                    response_state.get().has_value();
            });

            if (exit_) {
                LOGW("exit copy file: {}", cp_file_.file_.ref_path());
                return S_FALSE;
            }
            if (!resp_buffer_.has_value()) {
                LOGW("timeout waiting clipboard resp: {}", cp_file_.file_.ref_path());
                return S_FALSE;
            }
            if (req_index_ == resp_buffer_->req_index()) {
                break; // got the matching resp
            }
            // Stale resp (e.g. duplicated by the net channel); drop it and keep waiting.
            LOGW("stale clipboard resp index {}, expected {}, continue waiting",
                 resp_buffer_->req_index(), req_index_.load());
            resp_buffer_.reset();
        }

        // copy data
        auto resp_buffer = resp_buffer_.value();
        const auto read_size = static_cast<size_t>(resp_buffer.read_size());
        if (read_size > cb) {
            LOGE("clipboard response too large, req size: {}, resp size: {}", cb, read_size);
            return STG_E_READFAULT;
        }
        if (read_size > resp_buffer.buffer().size()) {
            LOGE("clipboard response buffer too small, declared: {}, actual: {}", read_size, resp_buffer.buffer().size());
            return STG_E_READFAULT;
        }
        if (read_size > 0) {
            memcpy(pv, resp_buffer.buffer().data(), resp_buffer.read_size());
            if (pcbRead) {
                *pcbRead = resp_buffer.read_size();
            }
            current_position_ += resp_buffer.read_size();
        }
        req_index_ += 1;

        // clear data
        resp_buffer_.reset();
        return S_OK;
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
        const auto expected_index = req_index_.load();
        if (rb.req_index() < expected_index) {
            LOGW("ignore completed clipboard resp index {}, expected {}",
                 rb.req_index(), expected_index);
            return;
        }
        if (resp_buffer_.has_value() &&
            resp_buffer_->req_index() == expected_index &&
            rb.req_index() != expected_index) {
            LOGW("keep matching clipboard resp index {}, ignore incoming {}",
                 expected_index, rb.req_index());
            return;
        }
        ClipboardRespBuffer buffer;
        buffer.CopyFrom(rb);
        resp_buffer_ = buffer;
        data_cv_.notify_all();
    }

    void CpFileStream::Exit() {
        exit_ = true;
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
