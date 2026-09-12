#pragma once

#include <atomic>
#include <memory>

namespace px {

class MessageListener;
class PxContext;

class PxConnectedManager final : public std::enable_shared_from_this<PxConnectedManager> {
  public:
    static std::shared_ptr<PxConnectedManager> Create(const std::shared_ptr<PxContext>& context);
    explicit PxConnectedManager(std::shared_ptr<PxContext> context);
    ~PxConnectedManager();

    int ConnectedClientCount() const noexcept;

  private:
    void RegisterMessageListener();

    std::shared_ptr<PxContext> context_{};
    std::shared_ptr<MessageListener> messageListener_{};
    std::atomic_int connectedClientCount_{0};
};

} // namespace px
