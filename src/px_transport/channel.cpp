#include "channel.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace px::transport {
namespace {

using Deadline = std::chrono::steady_clock::time_point;

std::uint32_t RemainingMilliseconds(Deadline deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    return remaining > 0 ? static_cast<std::uint32_t>(remaining) : 0;
}

std::expected<Bytes, Error> ReadExactly(const std::shared_ptr<Stream>& stream, std::size_t size, Deadline deadline) {
    Bytes output{};
    output.reserve(size);
    while (output.size() < size) {
        const auto timeout_ms = RemainingMilliseconds(deadline);
        if (!timeout_ms) return std::unexpected(Error::kTimeout);
        auto received = stream->Read(size - output.size(), timeout_ms);
        if (!received) return std::unexpected(received.error());
        if (received->empty()) return std::unexpected(Error::kClosed);
        output.insert(output.end(), received->begin(), received->end());
    }
    return output;
}

bool ValidKind(std::uint8_t kind) {
    return kind >= static_cast<std::uint8_t>(ChannelKind::kControl) && kind <= static_cast<std::uint8_t>(ChannelKind::kRdp);
}

std::int32_t Priority(ChannelKind kind) {
    switch (kind) {
        case ChannelKind::kInput:
            return 100;
        case ChannelKind::kControl:
            return 90;
        case ChannelKind::kRdp:
            return 50;
        case ChannelKind::kClipboard:
            return 10;
        case ChannelKind::kFile:
            return -100;
    }
    return 0;
}
}  // namespace

std::expected<std::shared_ptr<Channel>, Error> Channel::Open(const std::shared_ptr<Connection>& connection, ChannelKind kind,
                                                             std::uint32_t timeout_ms) {
    if (!connection || !ValidKind(static_cast<std::uint8_t>(kind))) return std::unexpected(Error::kInvalid);
    const auto stream = connection->OpenStream(Priority(kind), timeout_ms);
    if (!stream) return std::unexpected(Error::kFailed);
    const std::array<std::uint8_t, 8> header{'P', 'X', 'Q', 2, static_cast<std::uint8_t>(kind), 0, 0, 0};
    const auto sent = stream->Write(header, timeout_ms);
    if (!sent) return std::unexpected(sent.error());
    return std::make_shared<Channel>(stream, kind);
}

std::expected<std::shared_ptr<Channel>, Error> Channel::Accept(const std::shared_ptr<Connection>& connection, std::uint32_t timeout_ms) {
    if (!connection) return std::unexpected(Error::kInvalid);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    const auto stream = connection->AcceptStream(timeout_ms);
    if (!stream) return std::unexpected(Error::kFailed);
    const auto header = ReadExactly(stream, 8, deadline);
    if (!header) return std::unexpected(header.error());
    if ((*header)[0] != 'P' || (*header)[1] != 'X' || (*header)[2] != 'Q' || (*header)[3] != 2 || !ValidKind((*header)[4]) || (*header)[5] != 0 ||
        (*header)[6] != 0 || (*header)[7] != 0)
        return std::unexpected(Error::kInvalid);
    const auto kind = static_cast<ChannelKind>((*header)[4]);
    const auto prioritized = stream->SetPriority(Priority(kind), RemainingMilliseconds(deadline));
    if (!prioritized) return std::unexpected(prioritized.error());
    return std::make_shared<Channel>(stream, kind);
}

std::expected<void, Error> Channel::Send(std::span<const std::uint8_t> payload, std::uint32_t timeout_ms) {
    if (payload.empty() || payload.size() > kMaximumMessage) return std::unexpected(Error::kInvalid);
    std::lock_guard lock(send_mutex_);
    const auto message_size = static_cast<std::uint32_t>(payload.size());
    Bytes framed{};
    framed.reserve(payload.size() + 4);
    for (int byte_shift = 24; byte_shift >= 0; byte_shift -= 8) framed.push_back(static_cast<std::uint8_t>(message_size >> byte_shift));
    framed.insert(framed.end(), payload.begin(), payload.end());
    // The ABI bounds each write to 1 MiB. Split only the largest legal message, retaining serialization across both writes.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::size_t offset{};
    while (offset < framed.size()) {
        const auto chunk_size = std::min(kMaximumMessage, framed.size() - offset);
        const auto sent = stream_->Write(std::span<const std::uint8_t>{framed}.subspan(offset, chunk_size), RemainingMilliseconds(deadline));
        if (!sent) return std::unexpected(sent.error());
        offset += *sent;
    }
    return {};
}

std::expected<Bytes, Error> Channel::Receive(std::uint32_t timeout_ms) {
    std::lock_guard lock(receive_mutex_);
    if (receive_failed_) return std::unexpected(Error::kClosed);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const auto target_size = receive_message_size_.value_or(4);
        if (receive_buffer_.size() < target_size) {
            const auto remaining_ms = RemainingMilliseconds(deadline);
            if (!remaining_ms) return std::unexpected(Error::kTimeout);
            auto fragment = stream_->Read(target_size - receive_buffer_.size(), remaining_ms);
            if (!fragment) {
                receive_failed_ = fragment.error() != Error::kTimeout;
                return std::unexpected(fragment.error());
            }
            receive_buffer_.insert(receive_buffer_.end(), fragment->begin(), fragment->end());
            continue;
        }
        if (receive_message_size_) {
            receive_message_size_.reset();
            if (receiving_file_receipt_) {
                receiving_file_receipt_ = false;
                std::uint64_t received_bytes{};
                for (const auto receipt_byte : receive_buffer_) received_bytes = (received_bytes << 8) | receipt_byte;
                receive_buffer_.clear();
                if (!file_receipt_handler_(received_bytes)) {
                    receive_failed_ = true;
                    return std::unexpected(Error::kInvalid);
                }
                continue;
            }
            return std::exchange(receive_buffer_, {});
        }
        std::size_t message_size{};
        for (const auto header_byte : receive_buffer_) message_size = (message_size << 8) | header_byte;
        receive_buffer_.clear();
        if (message_size == 0 && kind_ == ChannelKind::kControl && file_receipt_handler_) {
            receiving_file_receipt_ = true;
            receive_message_size_ = sizeof(std::uint64_t);
            continue;
        }
        if (message_size == 0 || message_size > kMaximumMessage) {
            receive_failed_ = true;
            return std::unexpected(Error::kInvalid);
        }
        receive_message_size_ = message_size;
        receive_buffer_.reserve(message_size);
    }
}

std::expected<void, Error> Channel::SendFileReceipt(std::uint64_t received_bytes, std::uint32_t timeout_ms) {
    if (kind_ != ChannelKind::kControl) return std::unexpected(Error::kInvalid);
    std::array<std::uint8_t, 12> record{};
    for (std::size_t byte_index{}; byte_index < sizeof(received_bytes); ++byte_index)
        record[4 + byte_index] = static_cast<std::uint8_t>(received_bytes >> ((7 - byte_index) * 8));
    std::lock_guard lock(send_mutex_);
    const auto written = stream_->Write(record, timeout_ms);
    if (!written) return std::unexpected(written.error());
    return {};
}

std::expected<void, Error> Channel::Finish(std::uint32_t timeout_ms) {
    std::lock_guard lock(send_mutex_);
    return stream_->Finish(timeout_ms);
}
}  // namespace px::transport
