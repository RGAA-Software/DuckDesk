#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace px::panel::product {

class PanelWorker final {
  public:
    static std::shared_ptr<PanelWorker> Create();
    PanelWorker();
    ~PanelWorker();

    PanelWorker(const PanelWorker&) = delete;
    PanelWorker& operator=(const PanelWorker&) = delete;

    bool Post(std::function<void()> task);
    void Stop();

  private:
    struct State final {
        std::mutex mutex{};
        std::condition_variable_any wakeup{};
        std::deque<std::function<void()>> tasks{};
        bool accepting{true};
    };

    explicit PanelWorker(std::shared_ptr<State> state);
    static void Run(const std::shared_ptr<State>& state, std::stop_token stopToken);

    std::shared_ptr<State> state_{};
    std::jthread thread_{};
};

} // namespace px::panel::product
