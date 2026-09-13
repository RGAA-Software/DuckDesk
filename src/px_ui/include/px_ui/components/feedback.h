#pragma once

#include "px_ui/widget_types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace px::ui {

enum class FeedbackVariant { Info, Success, Warning, Error };

struct ToastMessage final {
    std::string title{};
    std::string description{};
    FeedbackVariant variant{FeedbackVariant::Info};
    std::chrono::milliseconds duration{3500};
};

class ToastHost final {
  public:
    void Push(ToastMessage message);
    void Draw();
    void Clear() noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;

  private:
    struct Entry final {
        std::uint64_t id{0};
        ToastMessage message{};
        std::chrono::steady_clock::time_point expiresAt{};
    };

    std::deque<Entry> entries_{};
    std::uint64_t nextId_{1};
};

void InlineAlert(std::string_view title, std::string_view description, FeedbackVariant variant);

} // namespace px::ui
