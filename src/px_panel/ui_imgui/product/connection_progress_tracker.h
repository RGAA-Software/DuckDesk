#pragma once

#include "remote_control_port.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace px::panel::product {

class ConnectionProgressTracker final {
  public:
    [[nodiscard]] std::optional<std::uint64_t> Begin(ui::ConnectionIntent intent, std::string target);
    void BeginStep(std::uint64_t generation, ui::ConnectionStepKind kind, std::string detail = {});
    void SucceedStep(std::uint64_t generation, ui::ConnectionStepKind kind, std::string detail = {});
    void Fail(std::uint64_t generation, ui::ConnectionStepKind kind, ui::ConnectionFailureReason reason, std::string diagnostic = {});
    void Complete(std::uint64_t generation, std::string detail = {});
    [[nodiscard]] std::optional<ui::ConnectionProgress> Snapshot() const;

  private:
    void SetStepState(std::uint64_t generation, ui::ConnectionStepKind kind, ui::ConnectionStepState state, std::string detail);

    mutable std::mutex mutex_{};
    std::optional<ui::ConnectionProgress> progress_{};
    std::uint64_t nextGeneration_{1};
};

} // namespace px::panel::product
