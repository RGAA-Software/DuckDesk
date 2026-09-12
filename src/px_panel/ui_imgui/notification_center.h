#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace px::panel::ui {

enum class NotificationLevel : std::uint8_t { Information, Error };

struct PanelNotification final {
    std::uint64_t id{};
    NotificationLevel level{NotificationLevel::Information};
    std::string title{};
    std::string message{};
    std::function<void()> action{};
    std::chrono::steady_clock::time_point createdAt{std::chrono::steady_clock::now()};
};

class NotificationCenter final {
  public:
    void Publish(PanelNotification notification);
    void Draw();

  private:
    std::mutex mutex_{};
    std::deque<PanelNotification> pending_{};
    std::deque<PanelNotification> active_{};
    std::uint64_t nextId_{1};
};

} // namespace px::panel::ui
