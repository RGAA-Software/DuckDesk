#pragma once

#include <string>

namespace px::render {

struct RenderLogContext final {
    std::string trace_token;
    std::string request_token;
    std::string logical_session_token;
    std::string connection_token;
    std::string stream_token;
    std::string transport_kind;

    [[nodiscard]] bool HasCorrelation() const noexcept {
        return !trace_token.empty() || !request_token.empty() ||
               !logical_session_token.empty() || !connection_token.empty() ||
               !stream_token.empty();
    }
};

}  // namespace px::render
