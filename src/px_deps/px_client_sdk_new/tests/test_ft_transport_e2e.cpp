#include <gtest/gtest.h>

#include <QCryptographicHash>
#include <QFile>
#include <QProcessEnvironment>
#include <QString>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "ft_async_session.h"
#include "px_common_new/data.h"
#include "px_common_new/message_notifier.h"
#include "px_message.pb.h"
#include "px_message_new/proto_converter.h"
#include "sdk_net_client.h"
#include "sdk_params.h"

namespace {

using namespace std::chrono_literals;

struct TransportTestState final {
    std::mutex mutex;
    std::condition_variable changed;
    bool connected = false;
    bool disconnected = false;
    std::weak_ptr<px::ft::FtAsyncSession> session;
    std::unordered_map<std::int32_t, std::string> completed_jobs;
    std::unordered_map<std::int32_t, std::string> completed_operations;
};

class LocalTestFiles final {
public:
    explicit LocalTestFiles(std::filesystem::path root) : root_(std::move(root)) {
        std::filesystem::create_directories(root_);
    }

    ~LocalTestFiles() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    LocalTestFiles(const LocalTestFiles&) = delete;
    LocalTestFiles& operator=(const LocalTestFiles&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
};

std::string RequiredEnvironment(const QProcessEnvironment& environment,
                                const QString& name) {
    const auto value = environment.value(name).toStdString();
    EXPECT_FALSE(value.empty())
        << "Missing environment variable: " << name.toStdString();
    return value;
}

std::int64_t IntegerEnvironment(const QProcessEnvironment& environment,
                                const QString& name,
                                std::int64_t fallback) {
    bool ok = false;
    const auto value = environment.value(name).toLongLong(&ok);
    return ok ? value : fallback;
}

void CreateDeterministicFile(const std::filesystem::path& path, std::uint64_t size) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    std::array<char, 64 * 1024> block{};
    std::uint32_t state = 0x6d2b79f5U;
    std::uint64_t remaining = size;
    while (remaining > 0) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, block.size()));
        for (std::size_t index = 0; index < count; ++index) {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            block[index] = static_cast<char>(32U + (state % 95U));
        }
        output.write(block.data(), static_cast<std::streamsize>(count));
        ASSERT_TRUE(output.good());
        remaining -= count;
    }
}

QByteArray Sha256(const std::filesystem::path& path) {
    QFile file(QString::fromStdWString(path.wstring()));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(1024 * 1024));
    }
    return hash.result().toHex();
}

bool WaitConnected(const std::shared_ptr<TransportTestState>& state,
                   std::chrono::milliseconds timeout) {
    std::unique_lock lock(state->mutex);
    return state->changed.wait_for(lock, timeout, [&] {
        return state->connected || state->disconnected;
    }) && state->connected;
}

bool WaitJob(const std::shared_ptr<TransportTestState>& state,
             std::int32_t id,
             std::chrono::milliseconds timeout,
             std::string& error) {
    std::unique_lock lock(state->mutex);
    if (!state->changed.wait_for(lock, timeout, [&] {
            return state->completed_jobs.contains(id) || state->disconnected;
        })) {
        error = "job deadline exceeded";
        return false;
    }
    if (state->disconnected && !state->completed_jobs.contains(id)) {
        error = "transport disconnected";
        return false;
    }
    error = state->completed_jobs.at(id);
    return error.empty();
}

bool WaitOperation(const std::shared_ptr<TransportTestState>& state,
                   std::int32_t id,
                   std::chrono::milliseconds timeout,
                   std::string& error) {
    std::unique_lock lock(state->mutex);
    if (!state->changed.wait_for(lock, timeout, [&] {
            return state->completed_operations.contains(id) || state->disconnected;
        })) {
        error = "operation deadline exceeded";
        return false;
    }
    if (state->disconnected && !state->completed_operations.contains(id)) {
        error = "transport disconnected";
        return false;
    }
    error = state->completed_operations.at(id);
    return error.empty();
}

} // namespace

TEST(FileTransferTransportE2E, UploadDownloadAndDelete) {
    const auto environment = QProcessEnvironment::systemEnvironment();
    const auto transport = environment.value(
        QStringLiteral("PX_FT_E2E_TRANSPORT")).toStdString();
    if (transport.empty()) {
        GTEST_SKIP() << "PX_FT_E2E_TRANSPORT is not configured";
    }
    ASSERT_TRUE(transport == "ws" || transport == "wss" ||
                transport == "relay" || transport == "udp_direct");

    const auto host = RequiredEnvironment(
        environment, QStringLiteral("PX_FT_E2E_HOST"));
    const auto remote_device_id = RequiredEnvironment(
        environment, QStringLiteral("PX_FT_E2E_REMOTE_DEVICE_ID"));
    const auto visitor_device_id = RequiredEnvironment(
        environment, QStringLiteral("PX_FT_E2E_VISITOR_DEVICE_ID"));
    const auto ticket = RequiredEnvironment(
        environment, QStringLiteral("PX_FT_E2E_TICKET"));
    const auto nonce = RequiredEnvironment(
        environment, QStringLiteral("PX_FT_E2E_NONCE"));
    ASSERT_FALSE(HasFailure());

    const auto port = static_cast<int>(IntegerEnvironment(
        environment, QStringLiteral("PX_FT_E2E_PORT"), 20371));
    const auto relay_host = environment.value(
        QStringLiteral("PX_FT_E2E_RELAY_HOST")).toStdString();
    const auto relay_port = static_cast<int>(IntegerEnvironment(
        environment, QStringLiteral("PX_FT_E2E_RELAY_PORT"), 0));
    const auto byte_count = static_cast<std::uint64_t>(
        IntegerEnvironment(environment, QStringLiteral("PX_FT_E2E_BYTES"),
                           16 * 1024 * 1024));
    const auto remote_dir = environment.value(
        QStringLiteral("PX_FT_E2E_REMOTE_DIR"),
        QStringLiteral("C:/Windows/Temp")).toStdString();
    const auto timeout = std::chrono::milliseconds(
        IntegerEnvironment(environment, QStringLiteral("PX_FT_E2E_TIMEOUT_MS"),
                           300000));
    if (transport == "relay") {
        ASSERT_FALSE(relay_host.empty());
        ASSERT_GT(relay_port, 0);
    }

    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    LocalTestFiles local_files(
        std::filesystem::temp_directory_path() / ("px_ft_transport_" + suffix));
    const auto source_path = local_files.root() / "source.bin";
    const auto downloaded_path = local_files.root() / "downloaded.bin";
    const auto remote_path = remote_dir + "/px_ft_transport_" + suffix + ".bin";
    CreateDeterministicFile(source_path, byte_count);
    ASSERT_FALSE(HasFailure());
    const auto source_sha = Sha256(source_path);
    ASSERT_FALSE(source_sha.isEmpty());

    const auto notifier = std::make_shared<px::MessageNotifier>();
    const auto params = std::make_shared<px::ThunderSdkParams>();
    params->ssl_ = transport == "wss";
    params->enable_audio_ = false;
    params->enable_video_ = false;
    params->enable_controller_ = false;
    params->file_transfer_only_ = true;
    params->ip_ = host;
    params->port_ = port;
    params->media_path_ = "/media?only_audio=0&remote_device_id=" + remote_device_id
        + "&stream_id=" + nonce + "&visitor_device_id=" + visitor_device_id;
    params->ft_path_ = "/file/transfer?remote_device_id=" + remote_device_id
        + "&stream_id=" + nonce + "&visitor_device_id=" + visitor_device_id;
    params->client_type_ = px::ClientType::kWindows;
    params->nt_type_ = transport == "relay"
        ? px::ClientNetworkType::kRelay
        : transport == "udp_direct"
            ? px::ClientNetworkType::kUdpDirect
            : px::ClientNetworkType::kWebsocket;
    params->bare_device_id_ = visitor_device_id;
    params->bare_remote_device_id_ = remote_device_id;
    params->device_id_ = "client_" + visitor_device_id + "_e2e";
    params->remote_device_id_ = "server_" + remote_device_id;
    params->ft_device_id_ = "ft_" + params->device_id_;
    params->ft_remote_device_id_ = "ft_" + params->remote_device_id_;
    params->stream_id_ = nonce;
    params->appkey_ = "test_appkey";
    params->relay_host_ = relay_host;
    params->relay_port_ = relay_port;
    params->relay_appkey_ = "test_appkey";
    params->connection_ticket_ = ticket;
    params->connection_nonce_ = nonce;

    const auto client = std::make_shared<px::NetClient>(
        params, notifier, host, port, params->media_path_, params->ft_path_,
        params->nt_type_, params->device_id_, params->remote_device_id_,
        params->ft_device_id_, params->ft_remote_device_id_, nonce);
    const auto state = std::make_shared<TransportTestState>();
    const std::weak_ptr<px::NetClient> weak_client = client;
    const std::weak_ptr<TransportTestState> weak_state = state;

    const auto session = px::ft::FtAsyncSession::Create(
        [weak_client, visitor_device_id, nonce](const auto& message) {
            const auto client_lock = weak_client.lock();
            if (!client_lock) {
                return px::FileTransferSendResult::Disconnected(
                    "transport test client was destroyed");
            }
            const auto outgoing = std::make_shared<px::Message>(*message);
            outgoing->set_type(outgoing->has_file_response()
                ? px::MessageType::kFileResponse
                : px::MessageType::kFileAction);
            outgoing->set_device_id(visitor_device_id);
            outgoing->set_stream_id(nonce);
            return client_lock->PostFileTransferMessage(px::ProtoAsData(outgoing));
        },
        [weak_state](const auto& engine) {
            const std::weak_ptr<px::ft::FtEngine> weak_engine = engine;
            engine->SetOverwriteConfirmCallback(
                [weak_engine](std::int32_t job_id, std::int32_t file_num,
                              const std::string&, bool, bool) {
                    if (const auto engine_lock = weak_engine.lock()) {
                        engine_lock->ConfirmFile(job_id, file_num, true, 0);
                    }
                });
            engine->SetJobDoneCallback(
                [weak_state](std::int32_t job_id, std::int32_t,
                             const std::string& error) {
                    if (const auto state_lock = weak_state.lock()) {
                        {
                            std::scoped_lock lock(state_lock->mutex);
                            state_lock->completed_jobs[job_id] = error;
                        }
                        state_lock->changed.notify_all();
                    }
                });
            engine->SetResponseCallback(
                [weak_state](const px::FileResponse& response) {
                    const auto state_lock = weak_state.lock();
                    if (!state_lock) return;
                    std::int32_t id = 0;
                    std::string error;
                    if (response.has_done()) {
                        id = response.done().id();
                    } else if (response.has_error()) {
                        id = response.error().id();
                        error = response.error().error();
                    } else {
                        return;
                    }
                    {
                        std::scoped_lock lock(state_lock->mutex);
                        state_lock->completed_operations[id] = error;
                    }
                    state_lock->changed.notify_all();
                });
        });
    ASSERT_TRUE(session->Start());
    state->session = session;

    client->SetOnConnectCallback([weak_state] {
        if (const auto state_lock = weak_state.lock()) {
            {
                std::scoped_lock lock(state_lock->mutex);
                state_lock->connected = true;
            }
            state_lock->changed.notify_all();
        }
    });
    client->SetOnDisconnectedCallback([weak_state] {
        if (const auto state_lock = weak_state.lock()) {
            {
                std::scoped_lock lock(state_lock->mutex);
                state_lock->disconnected = true;
            }
            state_lock->changed.notify_all();
        }
    });
    client->SetOnRawMessageCallback([weak_state](const auto& message) {
        const auto state_lock = weak_state.lock();
        const auto session_lock = state_lock ? state_lock->session.lock() : nullptr;
        if (!session_lock || (message->type() != px::MessageType::kFileAction
                && message->type() != px::MessageType::kFileResponse)) {
            return;
        }
        static_cast<void>(session_lock->Post(
            "transport-e2e-inbound", [message, nonce = message->stream_id()](const auto& engine) {
                if (message->type() == px::MessageType::kFileAction) {
                    engine->HandleFileAction(message->file_action(), nonce);
                } else {
                    engine->HandleFileResponse(message->file_response());
                }
            }));
    });

    client->Start();
    ASSERT_TRUE(WaitConnected(state, 30s));

    const auto upload_id = std::make_shared<std::int32_t>(0);
    ASSERT_TRUE(session->PostAndWait(
        "transport-e2e-upload",
        [upload_id, source = source_path.string(), remote_path](const auto& engine) {
            *upload_id = engine->SendFiles(source, false, remote_path);
            engine->SetOverwriteStrategy(*upload_id, true);
        }, 5s));
    std::string error;
    ASSERT_TRUE(WaitJob(state, *upload_id, timeout, error)) << error;

    const auto download_id = std::make_shared<std::int32_t>(0);
    ASSERT_TRUE(session->PostAndWait(
        "transport-e2e-download",
        [download_id, remote_path, local = downloaded_path.string()](const auto& engine) {
            *download_id = engine->ReceiveFiles(remote_path, false, local);
            engine->SetOverwriteStrategy(*download_id, true);
        }, 5s));
    ASSERT_TRUE(WaitJob(state, *download_id, timeout, error)) << error;
    ASSERT_TRUE(std::filesystem::exists(downloaded_path));
    const auto downloaded_sha = Sha256(downloaded_path);
    ASSERT_EQ(source_sha, downloaded_sha);

    constexpr std::int32_t kRemoveOperationId = -900001;
    ASSERT_TRUE(session->PostAndWait(
        "transport-e2e-remove",
        [remote_path, operation_id = kRemoveOperationId](const auto& engine) {
            engine->RemoveFile(operation_id, remote_path);
        }, 5s));
    ASSERT_TRUE(WaitOperation(state, kRemoveOperationId, 30s, error)) << error;

    const auto statistics = session->GetStatistics();
    EXPECT_GT(statistics.accepted_messages, 0U);
    if (IntegerEnvironment(environment,
                           QStringLiteral("PX_FT_E2E_REQUIRE_BUSY"), 0) != 0) {
        EXPECT_GT(statistics.busy_retries, 0U);
        EXPECT_GT(statistics.writable_waits, 0U);
    }
    EXPECT_EQ(statistics.disconnected_retries, 0U);
    EXPECT_EQ(statistics.transport_errors, 0U);

    std::cout << "FT_TRANSPORT_E2E PASS transport=" << transport
              << " bytes=" << byte_count
              << " sha256=" << source_sha.toStdString()
              << " accepted=" << statistics.accepted_messages
              << " busy=" << statistics.busy_retries
              << " waits=" << statistics.writable_waits
              << " wakeups=" << statistics.writable_wakeups
              << std::endl;

    ASSERT_TRUE(session->StopAndWait(5s));
    client->Exit();
    notifier->Stop(px::MessageBusStopMode::kCancel);
}
