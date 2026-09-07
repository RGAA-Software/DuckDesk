#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <QObject>

namespace asio2 {
class timer;
}

namespace px {

class Thread;

class ClientModuleContext final : public QObject,
                                  public std::enable_shared_from_this<ClientModuleContext> {
public:
    explicit ClientModuleContext(const std::string& module_name);
    ~ClientModuleContext() override;

    void Stop();
    void PostWorkTask(std::function<void()>&& task);
    void PostUITask(std::function<void()>&& task);
    void PostDelayTask(std::function<void()>&& task, int delay_ms);
    void StartTimer(int millis, std::function<void()>&& callback);

private:
    mutable std::mutex lifecycle_mutex_;
    std::shared_ptr<Thread> work_thread_;
    std::shared_ptr<asio2::timer> timer_;
    std::atomic<int> delay_task_id_ = 0;
    std::atomic_bool stopped_ = false;
};

}  // namespace px
