#include "panel_worker.h"

#include <utility>

namespace px::panel::product {

std::shared_ptr<PanelWorker> PanelWorker::Create() {
    return std::make_shared<PanelWorker>();
}

PanelWorker::PanelWorker() : PanelWorker{std::make_shared<State>()} {}

PanelWorker::PanelWorker(std::shared_ptr<State> state)
    : state_{std::move(state)}, thread_{[state = state_](const std::stop_token token) { Run(state, token); }} {}

PanelWorker::~PanelWorker() {
    Stop();
}

bool PanelWorker::Post(std::function<void()> task) {
    if (!task || !state_)
        return false;
    {
        const std::scoped_lock lock{state_->mutex};
        if (!state_->accepting)
            return false;
        state_->tasks.push_back(std::move(task));
    }
    state_->wakeup.notify_one();
    return true;
}

void PanelWorker::Stop() {
    if (!state_)
        return;
    {
        const std::scoped_lock lock{state_->mutex};
        state_->accepting = false;
        state_->tasks.clear();
    }
    thread_.request_stop();
    state_->wakeup.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void PanelWorker::Run(const std::shared_ptr<State>& state, const std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        std::function<void()> task{};
        {
            std::unique_lock lock{state->mutex};
            state->wakeup.wait(lock, stopToken, [&state] { return !state->tasks.empty() || !state->accepting; });
            if (stopToken.stop_requested() || !state->accepting)
                break;
            task = std::move(state->tasks.front());
            state->tasks.pop_front();
        }
        try {
            task();
        } catch (...) {
        }
    }
}

} // namespace px::panel::product
