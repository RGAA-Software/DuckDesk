#include "live_pusher_runtime.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "px_common_new/data.h"
#include "px_common_new/log.h"

namespace px {

void LivePusherRuntime::KeyframeChannel::Set(KeyframeRequester callback) {
    std::lock_guard lock(mutex);
    if (accepting) {
        requester = std::move(callback);
    }
}

void LivePusherRuntime::KeyframeChannel::Clear() {
    std::lock_guard lock(mutex);
    requester = {};
}

void LivePusherRuntime::KeyframeChannel::Disable() {
    std::lock_guard lock(mutex);
    accepting = false;
    requester = {};
}

void LivePusherRuntime::KeyframeChannel::Request() {
    KeyframeRequester callback;
    {
        std::lock_guard lock(mutex);
        if (!accepting) {
            return;
        }
        callback = requester;
    }
    if (callback) {
        callback();
    }
}

std::shared_ptr<LivePusherRuntime> LivePusherRuntime::Make(
    Config config,
    ProcessorFactory processor_factory) {
    if (!processor_factory) {
        return {};
    }
    auto channel = std::make_shared<KeyframeChannel>();
    const auto weak_channel = std::weak_ptr<KeyframeChannel>(channel);
    auto processor = processor_factory(config, [weak_channel] {
        if (const auto locked = weak_channel.lock()) {
            locked->Request();
        }
    });
    if (!processor) {
        return {};
    }
    auto state = std::make_shared<WorkerState>();
    state->processor = std::move(processor);
    auto runtime = std::make_shared<LivePusherRuntime>(
        ConstructionToken{}, std::move(config), std::move(channel),
        std::move(state));
    runtime->StartWorker();
    return runtime;
}

LivePusherRuntime::LivePusherRuntime(
    ConstructionToken,
    Config config,
    std::shared_ptr<KeyframeChannel> keyframe_channel,
    std::shared_ptr<WorkerState> worker_state)
    : config_(std::move(config)),
      keyframe_channel_(std::move(keyframe_channel)),
      worker_state_(std::move(worker_state)),
      selected_monitor_(config_.primary_monitor),
      monitor_selected_(!config_.primary_monitor.empty()) {}

LivePusherRuntime::~LivePusherRuntime() {
    Shutdown();
}

void LivePusherRuntime::StartWorker() {
    const auto state = worker_state_;
    worker_ = std::jthread([state](std::stop_token stop_token) {
        WorkerMain(state, stop_token);
    });
}

void LivePusherRuntime::SetKeyframeRequester(KeyframeRequester requester) {
    keyframe_channel_->Set(std::move(requester));
}

void LivePusherRuntime::ClearKeyframeRequester() {
    keyframe_channel_->Clear();
}

bool LivePusherRuntime::IsSelectedMonitor(const std::string& monitor_name) {
    std::lock_guard lock(monitor_mutex_);
    if (!monitor_selected_) {
        selected_monitor_ = monitor_name;
        monitor_selected_ = true;
        LOGI("LivePusher selected first active monitor as primary: {}",
             selected_monitor_.empty() ? "<game-hook>" : selected_monitor_);
    }
    return selected_monitor_ == monitor_name;
}

void LivePusherRuntime::EnqueueVideo(
    const std::string& monitor_name,
    PxPluginEncodedVideoType video_type,
    const std::shared_ptr<Data>& data,
    int width,
    int height,
    bool key,
    int64_t timestamp_ms) {
    if (!accepting_.load() || !data || data->Size() <= 0 ||
        !IsSelectedMonitor(monitor_name)) {
        return;
    }
    if (video_type != PxPluginEncodedVideoType::kH264 &&
        video_type != PxPluginEncodedVideoType::kH265) {
        return;
    }
    Enqueue(Entry{
        .kind = Entry::Kind::Video,
        .data = data,
        .video_type = video_type,
        .width = width,
        .height = height,
        .key = key,
        .timestamp_ms = timestamp_ms,
    });
}

void LivePusherRuntime::EnqueueAudio(
    const std::shared_ptr<Data>& data,
    int sample_rate,
    int channels,
    int bits,
    int64_t timestamp_ms) {
    if (!accepting_.load() || !data || data->Size() <= 0) {
        return;
    }
    Enqueue(Entry{
        .kind = Entry::Kind::Audio,
        .data = data,
        .sample_rate = sample_rate,
        .channels = channels,
        .bits = bits,
        .timestamp_ms = timestamp_ms,
    });
}

void LivePusherRuntime::Enqueue(Entry entry) {
    {
        std::lock_guard lock(worker_state_->mutex);
        if (!accepting_.load() || worker_state_->shutting_down) {
            return;
        }
        if (worker_state_->queue.size() >= kMaxQueue) {
            if (entry.kind == Entry::Kind::Video && entry.key) {
                const auto replaceable = std::find_if(
                    worker_state_->queue.begin(), worker_state_->queue.end(),
                    [](const Entry& queued) {
                        return queued.kind == Entry::Kind::Video && !queued.key;
                    });
                if (replaceable == worker_state_->queue.end()) {
                    ++dropped_;
                    return;
                }
                worker_state_->queue.erase(replaceable);
            } else {
                ++dropped_;
                return;
            }
        }
        worker_state_->queue.push_back(std::move(entry));
    }
    worker_state_->condition.notify_one();
}

void LivePusherRuntime::On1Second(int64_t now_ms) {
    if (dropped_.load() == 0) {
        return;
    }
    auto previous = last_drop_log_ms_.load();
    if (now_ms - previous < 10000 ||
        !last_drop_log_ms_.compare_exchange_strong(previous, now_ms)) {
        return;
    }
    LOGW("LivePusher dropped {} queued media entries", dropped_.exchange(0));
}

bool LivePusherRuntime::IsAccepting() const {
    return accepting_.load();
}

void LivePusherRuntime::Shutdown() {
    std::lock_guard shutdown_lock(shutdown_mutex_);
    if (accepting_.exchange(false)) {
        keyframe_channel_->Disable();
        {
            std::lock_guard state_lock(worker_state_->mutex);
            worker_state_->shutting_down = true;
        }
        worker_state_->condition.notify_all();
        worker_.request_stop();
    }
    if (worker_.joinable()) {
        if (worker_.get_id() == std::this_thread::get_id()) {
            return;
        }
        worker_.join();
    }
}

void LivePusherRuntime::WorkerMain(
    const std::shared_ptr<WorkerState>& state,
    std::stop_token stop_token) {
    while (true) {
        Entry entry;
        {
            std::unique_lock lock(state->mutex);
            state->condition.wait(lock, stop_token, [&state] {
                return state->shutting_down || !state->queue.empty();
            });
            if (state->queue.empty()) {
                if (state->shutting_down || stop_token.stop_requested()) {
                    break;
                }
                continue;
            }
            entry = std::move(state->queue.front());
            state->queue.pop_front();
        }
        try {
            if (entry.kind == Entry::Kind::Video) {
                state->processor->ProcessVideo(
                    entry.data, entry.video_type, entry.width, entry.height,
                    entry.key, entry.timestamp_ms);
            } else {
                state->processor->ProcessAudio(
                    entry.data, entry.sample_rate, entry.channels, entry.bits,
                    entry.timestamp_ms);
            }
        } catch (const std::exception& error) {
            LOGE("LivePusher processor exception: {}", error.what());
        } catch (...) {
            LOGE("LivePusher processor exception: unknown exception");
        }
    }
    try {
        state->processor->Close();
    } catch (const std::exception& error) {
        LOGE("LivePusher close exception: {}", error.what());
    } catch (...) {
        LOGE("LivePusher close exception: unknown exception");
    }
}

}  // namespace px
