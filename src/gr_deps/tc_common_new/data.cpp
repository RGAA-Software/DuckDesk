#include "data.h"
#include <cstring>

#include "file.h"
#include "memory_stat.h"
#include "tc_common_new/snowflake_id.h"

namespace tc
{

    Data::Data(const char* src, int64_t size) {
        this->data_ = static_cast<char *>(malloc(size));
        if (src) {
            memcpy(this->data_, src, size);
        }
        this->size_ = size;

#if MEMORY_STST_ON
        id_ = SnowflakeId::generate().implode();
        MemoryStat::Instance()->AddMemInfo(id_, std::make_shared<MemoryInfo>(MemoryInfo {
            .id_ = id_,
            .size_ = (uint64_t)size,
            .module_ = "",
            .name_ = "data"
        }));
#endif
    }

    std::shared_ptr<Data> Data::From(const std::string& data) {
        return std::make_shared<Data>(data.data(), static_cast<int64_t>(data.size()));
    }

    Data::~Data() {
        if (this->data_) {
            free(this->data_);
#if MEMORY_STST_ON
      MemoryStat::Instance()->RemoveMemInfo(id_);
#endif

        }
    }

    const char *Data::CStr() const {
        return this->data_;
    }

    std::string Data::AsString() const {
        return std::string(data_, size_);
    }

    void Data::ConvertToStr(std::string& out) const {
        out.resize(size_);
        memcpy((char*)out.data(), this->data_, size_);
    }

    int64_t Data::Size() const {
        return this->size_;
    }

    std::shared_ptr<Data> Data::Make(const char *data_, int64_t size) {
        return std::make_shared<Data>(data_, size);
    }

    char Data::At(int64_t offset) const {
        if (!this->data_ || offset < 0 || offset >= this->size_) {
            return 0;
        }
        return *(this->data_ + offset);
    }

    char* Data::DataAddr() const {
        return this->data_;
    }

    std::shared_ptr<Data> Data::Dup() const {
        return Data::Make(this->data_, this->size_);
    }

    bool Data::Append(char* data, int64_t size) {
        if (offset_ + size > size_) {
            return false;
        }
        memcpy(data_ + offset_, data, size);
        offset_ += size;
        return true;
    }

    int64_t Data::Offset() const {
        return offset_;
    }

    void Data::Reset() {
        offset_ = 0;
    }

    void Data::Save(const U8Path& path) {
        auto file = File::OpenForWriteB(path);
        if (file) {
            file->Write(0, data_, size_);
            file->Close();
        }
    }

    std::shared_ptr<Data> Data::Clone() const {
        if (this->data_ && this->size_ > 0) {
            return Data::Make(this->CStr(), this->Size());
        }
        return nullptr;
    }

}
