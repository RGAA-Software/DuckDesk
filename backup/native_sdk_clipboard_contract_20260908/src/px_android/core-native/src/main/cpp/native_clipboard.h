#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace px {
class Data;
class Message;
} // namespace px

namespace pixels::android {

struct NativeClipboardFile final {
    std::string display_name{};
    std::string transfer_name{};
    std::string backing_path{};
    std::int64_t size{};
};

struct NativeClipboardFiles final {
    std::string generation{};
    std::vector<NativeClipboardFile> files{};
};

class NativeClipboard final : public std::enable_shared_from_this<NativeClipboard> {
  public:
    using SendCallback = std::function<bool(std::shared_ptr<px::Data>)>;
    using TaskPoster = std::function<bool(std::function<void()>)>;
    using FilesCallback = std::function<void(const NativeClipboardFiles&)>;
    using DownloadCallback = std::function<void(const std::string&, const std::vector<std::string>&, const std::string&)>;

    static std::shared_ptr<NativeClipboard> Create(std::string device_id, std::string stream_id, SendCallback send_control,
                                                   SendCallback send_file, TaskPoster task_poster, FilesCallback files_callback,
                                                   DownloadCallback download_callback);

    NativeClipboard(std::string device_id, std::string stream_id, SendCallback send_control, SendCallback send_file, TaskPoster task_poster,
                    FilesCallback files_callback, DownloadCallback download_callback);
    ~NativeClipboard();

    NativeClipboard(const NativeClipboard&) = delete;
    NativeClipboard& operator=(const NativeClipboard&) = delete;

    [[nodiscard]] bool PublishLocalFiles(std::string generation, std::vector<NativeClipboardFile> files);
    void AcceptRemoteFiles(const std::shared_ptr<px::Message>& message);
    void HandleFileMessage(const std::shared_ptr<px::Message>& message);
    [[nodiscard]] bool DownloadRemoteFiles(const std::string& generation, const std::string& destination_directory);
    void Stop();

  private:
    struct ClipboardChunk final {
        std::string transfer_name{};
        std::int64_t request_index{};
        std::int64_t request_start{};
        std::int64_t requested_size{};
        std::vector<std::uint8_t> bytes{};
    };

    void RespondToBufferRequest(const std::shared_ptr<px::Message>& message);
    void RunDownload(std::stop_token stop_token, NativeClipboardFiles files, std::string destination_directory);
    [[nodiscard]] bool SendTransferBoundary(const NativeClipboardFile& file, bool begin, bool success) const;
    [[nodiscard]] std::optional<ClipboardChunk> RequestChunk(const NativeClipboardFile& file, std::int64_t offset, std::int64_t size,
                                                             std::stop_token stop_token);

    const std::string device_id_{};
    const std::string stream_id_{};
    const SendCallback send_control_{};
    const SendCallback send_file_{};
    const TaskPoster task_poster_{};
    const FilesCallback files_callback_{};
    const DownloadCallback download_callback_{};
    std::mutex mutex_{};
    NativeClipboardFiles local_files_{};
    NativeClipboardFiles remote_files_{};
    std::optional<ClipboardChunk> received_chunk_{};
    std::int64_t awaited_request_index_{};
    std::string awaited_transfer_name_{};
    std::condition_variable response_condition_{};
    std::jthread download_thread_{};
    bool download_active_{};
    bool stopped_{};
    std::int64_t next_request_index_{1};
};

} // namespace pixels::android
