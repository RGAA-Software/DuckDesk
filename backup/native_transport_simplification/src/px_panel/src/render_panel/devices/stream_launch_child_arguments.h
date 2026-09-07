#ifndef PX_STREAM_LAUNCH_CHILD_ARGUMENTS_H
#define PX_STREAM_LAUNCH_CHILD_ARGUMENTS_H

#include <string>
#include <vector>

#include "px_common/base64.h"

namespace px {

struct StreamLaunchChildCredentials final {
    std::string connection_ticket;
    std::string connection_nonce;
    std::string connection_instance_id;
};

inline std::vector<std::string> BuildStreamLaunchCredentialArguments(
    const StreamLaunchChildCredentials& credentials) {
    std::vector<std::string> arguments;
    if (!credentials.connection_ticket.empty()) {
        arguments.emplace_back(
            "--connection_ticket=" + Base64::Base64Encode(credentials.connection_ticket));
    }
    // IP-direct preparation intentionally has no Console ticket. Its nonce is
    // still part of the one-time stream binding and must reach the child.
    if (!credentials.connection_nonce.empty()) {
        arguments.emplace_back("--connection_nonce=" + credentials.connection_nonce);
    }
    if (!credentials.connection_instance_id.empty()) {
        arguments.emplace_back(
            "--connection_instance_id=" + credentials.connection_instance_id);
    }
    return arguments;
}

} // namespace px

#endif // PX_STREAM_LAUNCH_CHILD_ARGUMENTS_H
