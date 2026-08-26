#include "data.h"
#include <cstring>

#include "file.h"
#include "memory_stat.h"
#include "px_common_new/snowflake_id.h"

namespace px
{

    Data::Data(const char* src, int64_t size) {
        if (size > 0) {
            data_.resize(static_cast<std::size_t>(size));
        }
        if (src && !data_.empty()) {
            memcpy(data_.data(), src, data_.size());
        }

#if MEMORY_STST_ON
        id_ = SnowflakeId::generate().implode();
        MemoryStat::Instance()->AddMemInfo(id_, std::make_shared<MemoryInfo>(MemoryInfo {
            .id_ = id_,
            .size_ = static_cast<uint64_t>(data_.size()),
            .module_ = "",
            .name_ = "data"
        }));
#endif
    }

    std::shared_ptr<Data> Data::From(const std::string& data) {
        return std::make_shared<Data>(data.data(), static_cast<int64_t>(data.size()));
    }

    Data::~Data() {
#if MEMORY_STST_ON
        MemoryStat::Instance()->RemoveMemInfo(id_);
#endif
    }

    const char *Data::CStr() const {
        return data_.data();
    }

    std::string Data::AsString() const {
        return std::string(data_.begin(), data_.end());
    }

    void Data::ConvertToStr(std::string& out) const {
        out.assign(data_.begin(), data_.end());
    }

    int64_t Data::Size() const {
        return static_cast<int64_t>(data_.size());
    }

    std::shared_ptr<Data> Data::Make(const char *data_, int64_t size) {
        return std::make_shared<Data>(data_, size);
    }

    char Data::At(int64_t offset) const {
        if (offset < 0 || offset >= Size()) {
            return 0;
        }
        return data_[static_cast<std::size_t>(offset)];
    }

    char* Data::DataAddr() const {
        return const_cast<char*>(data_.data());
    }

    std::shared_ptr<Data> Data::Dup() const {
        return Data::Make(data_.data(), Size());
    }

    bool Data::Append(char* data, int64_t size) {
        if (!data || size < 0 || offset_ < 0 || offset_ + size > Size()) {
            return false;
        }
        memcpy(data_.data() + offset_, data, static_cast<std::size_t>(size));
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
            file->Write(0, data_.data(), data_.size());
            file->Close();
        }
    }

    std::shared_ptr<Data> Data::Clone() const {
        if (!data_.empty()) {
            return Data::Make(this->CStr(), this->Size());
        }
        return nullptr;
    }

}
