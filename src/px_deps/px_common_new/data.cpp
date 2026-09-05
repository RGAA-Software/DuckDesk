#include "data.h"

#include <algorithm>

namespace px {

Data::Data(std::size_t size) : data_(size) {}

Data::Data(std::span<const char> bytes) : Data(bytes.size()) {
    std::ranges::copy(bytes, data_.begin());
}

std::shared_ptr<Data> Data::Allocate(std::size_t size) {
    return std::make_shared<Data>(size);
}

std::shared_ptr<Data> Data::Copy(std::span<const char> bytes) {
    return std::make_shared<Data>(bytes);
}

std::shared_ptr<Data> Data::From(std::string_view text) {
    return Copy(std::span<const char>{text});
}

Data::~Data() = default;

std::span<const char> Data::Bytes() const noexcept {
    return data_;
}

std::span<char> Data::MutableBytes() noexcept {
    return data_;
}

std::string Data::AsString() const {
    return std::string{data_.begin(), data_.end()};
}

void Data::ConvertToStr(std::string& out) const {
    out.assign(data_.begin(), data_.end());
}

std::size_t Data::Size() const noexcept {
    return data_.size();
}

char Data::At(std::size_t offset) const noexcept {
    return offset < data_.size() ? data_[offset] : '\0';
}

std::shared_ptr<Data> Data::Dup() const {
    return Copy(data_);
}

bool Data::Append(std::span<const char> bytes) {
    if (offset_ > data_.size() || bytes.size() > data_.size() - offset_) {
        return false;
    }

    std::ranges::copy(bytes, data_.begin() + static_cast<std::ptrdiff_t>(offset_));
    offset_ += bytes.size();
    return true;
}

std::size_t Data::Offset() const noexcept {
    return offset_;
}

void Data::Reset() noexcept {
    offset_ = 0;
}

std::shared_ptr<Data> Data::Clone() const {
    return data_.empty() ? nullptr : Copy(data_);
}

}  // namespace px
