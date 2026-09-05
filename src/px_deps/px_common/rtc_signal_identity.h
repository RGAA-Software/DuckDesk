#ifndef PX_RTC_SIGNAL_IDENTITY_H
#define PX_RTC_SIGNAL_IDENTITY_H

#include <string>

namespace px {

inline std::string ResolveRtcSignalRemoteDeviceId(
    const std::string& remote_device_id,
    const std::string& console_signal_device_id) {
    if (!console_signal_device_id.empty()) {
        return console_signal_device_id;
    }
    return "server_" + remote_device_id;
}

inline std::string ResolveRtcFileTransferSignalRemoteDeviceId(
    const std::string& remote_device_id,
    const std::string& console_signal_device_id) {
    return "ft_" + ResolveRtcSignalRemoteDeviceId(
        remote_device_id, console_signal_device_id);
}

} // namespace px

#endif // PX_RTC_SIGNAL_IDENTITY_H
