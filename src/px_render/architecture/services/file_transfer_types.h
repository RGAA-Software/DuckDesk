#pragma once

#include <memory>
#include <string>

namespace px {

class Message;

// Owned transport-to-service envelope. Transport identity is copied as data;
// no network object or plug-in instance crosses the asynchronous boundary.
struct FileTransferInbound final {
    std::shared_ptr<Message> message;
    std::string logical_session_id;
    std::string transport_id;
    std::string connection_id;
};

// A late disconnect can only retire the route generation owned by the same
// transport connection.
struct FileTransferRouteDisconnected final {
    std::string logical_session_id;
    std::string stream_id;
    std::string transport_id;
    std::string connection_id;
};

}  // namespace px
