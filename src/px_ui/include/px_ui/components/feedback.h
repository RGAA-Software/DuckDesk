#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>

#include "px_ui/widget_types.h"

namespace px::ui {

enum class FeedbackVariant { Info, Success, Warning, Error };
enum class ToastPlacement { BottomRight, TopRight };

struct ToastMessage final {
    std::string title{};
    std::string description{};
    FeedbackVariant variant{FeedbackVariant::Info};
    std::chrono::milliseconds duration{3500};
    std::function<void()> onClick{};
};

class ToastHost final {
public:
    void Push(ToastMessage message);
    void Draw(ToastPlacement placement = ToastPlacement::BottomRight, float topInset = 0.0F);
    [[nodiscard]] bool CapturesPointer(float pointX, float pointY) const noexcept;
    void Clear() noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;

private:
    struct Entry final {
        std::uint64_t id{0};
        ToastMessage message{};
        std::chrono::steady_clock::time_point expiresAt{};
        ImVec2 minimum{};
        ImVec2 maximum{};
    };

    std::deque<Entry> entries_{};
    std::uint64_t nextId_{1};
};

void InlineAlert(std::string_view title, std::string_view description, FeedbackVariant variant);

}  // namespace px::ui
