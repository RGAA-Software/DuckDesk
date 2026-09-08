#include "native_clipboard.h"

#include "data.h"
#include "px_common/async_runtime.h"
#include "px_common/uuid.h"
#include "px_message.pb.h"
#include "proto_converter.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_set>
#include <utility>

namespace pixels::android {
namespace {

constexpr std::size_t kMaximumClipboardFiles = 16U;
constexpr std::int64_t kMaximumClipboardFileBytes = 512LL * 1024LL * 1024LL;
constexpr std::int64_t kMaximumClipboardTotalBytes = 1024LL * 1024LL * 1024LL;
constexpr std::int64_t kClipboardChunkBytes = px::kClipboardReadChunkBytes;
constexpr auto kChunkTimeout = std::chrono::seconds(5);

std::string SafeFileName(std::string name, const std::size_t index) {
    for (auto& character : name) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '/' || character == '\\' || character == ':' || character == '*' || character == '?' || character == '"' ||
            character == '<' || character == '>' || character == '|' || byte < 0x20U) {
            character = '_';
        }
    }
    if (name.empty() || name == "." || name == "..") {
        name = "pixels-clipboard-" + std::to_string(index + 1U);
    }
    constexpr std::size_t kMaximumNameBytes = 180U;
    if (name.size() > kMaximumNameBytes) {
        auto end = kMaximumNameBytes;
        while (end > 0 && (static_cast<unsigned char>(name[end]) & 0xc0U) == 0x80U) {
            --end;
        }
        name.resize(end);
    }
    return name;
}

bool ValidateFiles(const std::vector<NativeClipboardFile>& files, const bool require_backing_path) {
    if (files.empty() || files.size() > kMaximumClipboardFiles) {
        return false;
    }
    std::int64_t total_size{};
    std::unordered_set<std::string> transfer_names{};
    for (const auto& file : files) {
        if (!px::IsClipboardFileDescriptorValid(file.display_name, file.transfer_name, file.size) ||
            !transfer_names.insert(file.transfer_name).second || file.size > kMaximumClipboardFileBytes ||
            (require_backing_path && file.backing_path.empty()) || total_size > kMaximumClipboardTotalBytes - file.size) {
            return false;
        }
        total_size += file.size;
    }
    return true;
}

} // namespace

std::shared_ptr<NativeClipboard> NativeClipboard::Create(std::string device_id, std::string stream_id, SendCallback send_control,
                                                         SendCallback send_file, TaskPoster task_poster, FilesCallback files_callback,
                                                         DownloadCallback download_callback) {
    if (device_id.empty() || stream_id.empty() || !send_control || !send_file || !task_poster || !files_callback || !download_callback) {
        return {};
    }
    return std::make_shared<NativeClipboard>(std::move(device_id), std::move(stream_id), std::move(send_control), std::move(send_file),
                                             std::move(task_poster), std::move(files_callback), std::move(download_callback));
}

NativeClipboard::NativeClipboard(std::string device_id, std::string stream_id, SendCallback send_control, SendCallback send_file,
                                 TaskPoster task_poster, FilesCallback files_callback, DownloadCallback download_callback)
    : device_id_(std::move(device_id)), stream_id_(std::move(stream_id)), send_control_(std::move(send_control)), send_file_(std::move(send_file)),
      task_poster_(std::move(task_poster)), files_callback_(std::move(files_callback)), download_callback_(std::move(download_callback)) {}

NativeClipboard::~NativeClipboard() {
    Stop();
}

bool NativeClipboard::PublishLocalFiles(std::string generation, std::vector<NativeClipboardFile> files) {
    if (generation.empty() || generation.size() > 128U) {
        return false;
    }
    const auto offer_id = px::GetUUID();
    for (std::size_t index = 0; index < files.size(); ++index) {
        files[index].transfer_name = "pixels-clipboard://" + offer_id + "/" + std::to_string(index);
    }
    if (!ValidateFiles(files, true)) {
        return false;
    }
    for (const auto& file : files) {
        std::error_code error;
        const auto actual_size = std::filesystem::file_size(std::filesystem::path(file.backing_path), error);
        if (error || actual_size != static_cast<std::uintmax_t>(file.size)) {
            return false;
        }
    }
    {
        std::lock_guard lock(mutex_);
        if (stopped_) {
            return false;
        }
        local_files_ = {.generation = generation, .files = files};
    }

    px::Message message;
    message.set_type(px::kClipboardInfo);
    message.set_device_id(device_id_);
    message.set_stream_id(stream_id_);
    auto& clipboard = *message.mutable_clipboard_info();
    clipboard.set_type(px::kClipboardFiles);
    for (const auto& file : files) {
        auto& target = *clipboard.add_files();
        target.set_file_name(file.display_name);
        target.set_ref_path(file.display_name);
        target.set_full_path(file.transfer_name);
        target.set_total_size(file.size);
    }
    return send_control_(px::ProtoAsData(&message));
}

void NativeClipboard::AcceptRemoteFiles(const std::shared_ptr<px::Message>& message) {
    if (!message || message->type() != px::kClipboardInfo || !message->has_clipboard_info()) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        local_files_ = {};
        remote_files_ = {};
    }
    if (message->clipboard_info().type() != px::kClipboardFiles) {
        return;
    }
    NativeClipboardFiles files;
    files.generation = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    for (const auto& source : message->clipboard_info().files()) {
        files.files.push_back({.display_name = source.file_name(), .transfer_name = source.full_path(), .size = source.total_size()});
    }
    if (!ValidateFiles(files.files, false)) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        if (stopped_) {
            return;
        }
        remote_files_ = files;
    }
    files_callback_(files);
}

void NativeClipboard::RevokeLocalFiles() {
    std::lock_guard lock(mutex_);
    local_files_ = {};
}

void NativeClipboard::HandleFileMessage(const std::shared_ptr<px::Message>& message) {
    if (!message) {
        return;
    }
    if (message->type() == px::kClipboardReqBuffer && message->has_cp_req_buffer()) {
        const auto weak_self = weak_from_this();
        static_cast<void>(task_poster_([weak_self, message] {
            if (const auto self = weak_self.lock()) {
                self->RespondToBufferRequest(message);
            }
        }));
        return;
    }
    if (message->type() != px::kClipboardRespBuffer || !message->has_cp_resp_buffer()) {
        return;
    }
    const auto& response = message->cp_resp_buffer();
    std::lock_guard lock(mutex_);
    if (stopped_ || received_chunk_ || !pending_read_.Accepts(response)) {
        return;
    }
    received_chunk_ = ClipboardChunk{
        .transfer_name = response.full_name(),
        .request_index = response.req_index(),
        .request_start = response.req_start(),
        .requested_size = response.req_size(),
        .bytes = std::vector<std::uint8_t>(response.buffer().begin(), response.buffer().end()),
    };
    response_condition_.notify_all();
}

bool NativeClipboard::DownloadRemoteFiles(const std::string& generation, const std::string& destination_directory) {
    if (generation.empty() || destination_directory.empty()) {
        return false;
    }
    std::jthread completed_thread;
    NativeClipboardFiles files;
    {
        std::lock_guard lock(mutex_);
        if (stopped_ || download_active_ || remote_files_.generation != generation || !ValidateFiles(remote_files_.files, false)) {
            return false;
        }
        completed_thread = std::move(download_thread_);
        download_active_ = true;
        files = remote_files_;
    }
    if (completed_thread.joinable()) {
        if (completed_thread.get_id() == std::this_thread::get_id()) {
            px::PxAsyncRuntime::DeferJoin(std::move(completed_thread));
        } else {
            completed_thread.join();
        }
    }
    const auto weak_self = weak_from_this();
    std::lock_guard lock(mutex_);
    if (stopped_) {
        download_active_ = false;
        return false;
    }
    download_thread_ = std::jthread([weak_self, files = std::move(files), destination_directory](const std::stop_token stop_token) {
        if (const auto self = weak_self.lock()) {
            self->RunDownload(stop_token, files, destination_directory);
        }
    });
    return true;
}

void NativeClipboard::RespondToBufferRequest(const std::shared_ptr<px::Message>& message) {
    const auto& request = message->cp_req_buffer();
    NativeClipboardFile source;
    {
        std::lock_guard lock(mutex_);
        if (stopped_ || !px::ClipboardReadRequest::From(request).IsValid()) {
            return;
        }
        const auto iterator = std::ranges::find_if(local_files_.files, [&](const auto& file) { return file.transfer_name == request.full_name(); });
        if (iterator == local_files_.files.end()) {
            return;
        }
        source = *iterator;
    }

    const auto limit = px::ClipboardReadRequest::From(request).ReadSize(source.size);
    if (!limit) {
        return;
    }
    std::vector<char> bytes(static_cast<std::size_t>(*limit));
    std::ifstream input(std::filesystem::path(source.backing_path), std::ios::binary);
    std::int64_t read_size{};
    if (input && !bytes.empty()) {
        input.seekg(request.req_start());
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        read_size = static_cast<std::int64_t>(input.gcount());
    }

    {
        std::lock_guard lock(mutex_);
        if (stopped_ ||
            std::ranges::none_of(local_files_.files, [&request](const auto& file) { return file.transfer_name == request.full_name(); })) {
            return;
        }
    }
    px::Message response{};
    response.set_type(px::kClipboardRespBuffer);
    response.set_device_id(device_id_);
    response.set_stream_id(stream_id_);
    auto& buffer = *response.mutable_cp_resp_buffer();
    buffer.set_full_name(request.full_name());
    buffer.set_req_size(request.req_size());
    buffer.set_req_start(request.req_start());
    buffer.set_req_index(request.req_index());
    buffer.set_read_size(read_size);
    if (read_size > 0) {
        buffer.set_buffer(bytes.data(), static_cast<std::size_t>(read_size));
    }
    static_cast<void>(send_file_(px::ProtoAsData(&response)));
}

void NativeClipboard::RunDownload(const std::stop_token stop_token, NativeClipboardFiles files, std::string destination_directory) {
    std::vector<std::string> completed_paths;
    std::string error;
    std::unordered_set<std::string> used_names;
    std::error_code filesystem_error;
    const auto destination = std::filesystem::path(destination_directory);
    std::filesystem::create_directories(destination, filesystem_error);
    if (filesystem_error) {
        error = "destination_unavailable";
    }

    for (std::size_t index = 0; error.empty() && index < files.files.size() && !stop_token.stop_requested(); ++index) {
        const auto& file = files.files[index];
        auto name = SafeFileName(file.display_name, index);
        if (!used_names.insert(name).second) {
            name = std::to_string(index + 1U) + "_" + name;
            used_names.insert(name);
        }
        const auto output_path = destination / std::filesystem::path(name);
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output || !SendTransferBoundary(file, true, false)) {
            error = "transfer_start_failed";
            break;
        }
        std::int64_t offset{};
        while (offset < file.size && !stop_token.stop_requested()) {
            const auto requested = std::min(kClipboardChunkBytes, file.size - offset);
            const auto chunk = RequestChunk(file, offset, requested, stop_token);
            if (!chunk || chunk->request_start != offset || chunk->bytes.empty() ||
                chunk->bytes.size() > static_cast<std::size_t>(requested)) {
                error = "transfer_chunk_failed";
                break;
            }
            output.write(reinterpret_cast<const char*>(chunk->bytes.data()), static_cast<std::streamsize>(chunk->bytes.size()));
            if (!output) {
                error = "destination_write_failed";
                break;
            }
            offset += static_cast<std::int64_t>(chunk->bytes.size());
        }
        output.close();
        if (!output && error.empty()) {
            error = "destination_write_failed";
        }
        const bool success = error.empty() && !stop_token.stop_requested() && offset == file.size;
        static_cast<void>(SendTransferBoundary(file, false, success));
        if (!success) {
            std::filesystem::remove(output_path, filesystem_error);
            if (error.empty()) {
                error = "transfer_cancelled";
            }
            break;
        }
        completed_paths.push_back(output_path.string());
    }
    if (stop_token.stop_requested() && error.empty()) {
        error = "transfer_cancelled";
    }
    {
        std::lock_guard lock(mutex_);
        download_active_ = false;
        pending_read_.Cancel();
        received_chunk_.reset();
    }
    download_callback_(files.generation, completed_paths, error);
}

bool NativeClipboard::SendTransferBoundary(const NativeClipboardFile& file, const bool begin, const bool success) const {
    px::Message message;
    message.set_device_id(device_id_);
    message.set_stream_id(stream_id_);
    if (begin) {
        message.set_type(px::kClipboardReqAtBegin);
        message.mutable_cp_req_at_begin()->set_full_name(file.transfer_name);
    } else {
        message.set_type(px::kClipboardReqAtEnd);
        message.mutable_cp_req_at_end()->set_full_name(file.transfer_name);
        message.mutable_cp_req_at_end()->set_success(success);
    }
    return send_file_(px::ProtoAsData(&message));
}

std::optional<NativeClipboard::ClipboardChunk> NativeClipboard::RequestChunk(const NativeClipboardFile& file, const std::int64_t offset,
                                                                             const std::int64_t size,
                                                                             const std::stop_token stop_token) {
    std::optional<px::ClipboardReadRequest> pending{};
    {
        std::lock_guard lock(mutex_);
        if (stopped_) {
            return std::nullopt;
        }
        pending = pending_read_.Begin(file.transfer_name, offset, size);
        received_chunk_.reset();
    }
    if (!pending) {
        return {};
    }

    px::Message message;
    message.set_type(px::kClipboardReqBuffer);
    message.set_device_id(device_id_);
    message.set_stream_id(stream_id_);
    auto& request = *message.mutable_cp_req_buffer();
    request.set_full_name(file.transfer_name);
    request.set_req_size(size);
    request.set_req_start(offset);
    request.set_req_index(pending->index);
    if (!send_file_(px::ProtoAsData(&message))) {
        std::lock_guard lock(mutex_);
        pending_read_.Cancel();
        return std::nullopt;
    }

    std::unique_lock lock(mutex_);
    const auto self = shared_from_this();
    const auto received = response_condition_.wait_for(
        lock, kChunkTimeout, [self, stop_token] { return self->stopped_ || stop_token.stop_requested() || self->received_chunk_.has_value(); });
    pending_read_.Cancel();
    if (!received || stopped_ || stop_token.stop_requested() || !received_chunk_) {
        return std::nullopt;
    }
    auto result = std::move(received_chunk_);
    received_chunk_.reset();
    return result;
}

void NativeClipboard::Stop() {
    std::jthread thread;
    {
        std::lock_guard lock(mutex_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
        pending_read_.Cancel();
        download_thread_.request_stop();
        response_condition_.notify_all();
        thread = std::move(download_thread_);
        local_files_ = {};
        remote_files_ = {};
    }
    if (thread.joinable()) {
        if (thread.get_id() == std::this_thread::get_id()) {
            px::PxAsyncRuntime::DeferJoin(std::move(thread));
        } else {
            thread.join();
        }
    }
}

} // namespace pixels::android
