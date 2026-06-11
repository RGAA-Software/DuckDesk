//
// Created by RGAA on 8/04/2025.
//

#include "cp_file_stream.h"
#include "tc_common_new/log.h"

namespace tc
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
        if (!pv) {
            return STG_E_INVALIDPOINTER;
        }
        if (pcbRead) {
            *pcbRead = 0;
        }
        if (exit_ || !request_buffer_cb_ || !lifetime_token_ || !lifetime_token_->load()) {
            return S_FALSE;
        }
        if (!request_buffer_cb_(cp_file_, req_index_.load(), current_position_.load(), cb)) {
            return S_FALSE;
        }

        std::unique_lock lk(wait_data_mtx_);
        data_cv_.wait_for(lk, std::chrono::seconds(10), [this]() -> bool {
            return resp_buffer_.has_value();
        });

        if (exit_ || !resp_buffer_.has_value()) {
            LOGW("exit copy file: {}", cp_file_.file_.ref_path());
            return S_FALSE;
        }

        if (req_index_ != resp_buffer_->req_index()) {
            LOGE("invalid req index, send: {}, received: {}", req_index_.load(), resp_buffer_->req_index());
            return S_FALSE;
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
}
