#include "rdp_workspace.h"

#include "rdp_session.h"
#include "rdp_view.h"
#include "rdp_ui_queue.h"
#include "px_common/message_notifier.h"
#include "px_common/log.h"
#include "px_common/secret_buffer.h"
#include "px_common/url_helper.h"
#include "px_common/win32/unique_win_handle.h"
#include "px_client_sdk/sdk_net_client.h"
#include "px_client_sdk/sdk_messages.h"
#include "px_rdp/rdp_client_endpoint.h"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <mutex>

namespace px::rdp {
namespace {

using Json = nlohmann::json;

std::shared_ptr<SecretBuffer> ReadLaunchPipe() {
    const auto input = UniqueWinHandle{GetStdHandle(STD_INPUT_HANDLE)}; // Inherited process-owned stdin, closed after one handoff.
    if (!input || input.get() == INVALID_HANDLE_VALUE || GetFileType(input.get()) != FILE_TYPE_PIPE) {
        return {};
    }
    std::string bytes{};
    bytes.reserve(65536);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool complete{false};
    while (std::chrono::steady_clock::now() < deadline) {
        DWORD available{};
        if (!PeekNamedPipe(input.get(), nullptr, 0, nullptr, &available, nullptr)) {
            complete = GetLastError() == ERROR_BROKEN_PIPE;
            break;
        }
        if (available > 65536 - bytes.size()) {
            break;
        }
        if (available != 0) {
            const auto offset = bytes.size();
            bytes.resize(offset + available);
            DWORD read{};
            if (!ReadFile(input.get(), bytes.data() + offset, available, &read, nullptr) || read != available) {
                break;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    auto secret = SecretBuffer::Take(std::move(bytes));
    return complete && !secret->Bytes().empty() ? secret : nullptr;
}

class ClipboardMimeData final : public QMimeData {
  public:
    explicit ClipboardMimeData(std::shared_ptr<const ClipboardData> data) : data_(std::move(data)) {
        if (data_->text) {
            setText(*data_->text);
        }
        if (data_->html) {
            setHtml(*data_->html);
        }
        if (!data_->image.isNull()) {
            setImageData(data_->image);
        }
        if (!data_->urls.isEmpty()) {
            setUrls(data_->urls);
        }
    }

  private:
    std::shared_ptr<const ClipboardData> data_{};
};

class Workspace final : public std::enable_shared_from_this<Workspace> {
    struct EndpointSlot final {
        std::mutex mutex{};
        std::shared_ptr<RdpClientEndpoint> endpoint{};
        bool stopped{false};
        void Set(std::shared_ptr<RdpClientEndpoint> value) {
            std::lock_guard lock(mutex);
            if (stopped) {
                if (value) {
                    value->Stop();
                }
            } else {
                endpoint = std::move(value);
            }
        }
        std::shared_ptr<RdpClientEndpoint> Get() {
            std::lock_guard lock(mutex);
            return endpoint;
        }
        void Stop() {
            std::lock_guard lock(mutex);
            stopped = true;
            if (endpoint) {
                endpoint->Stop();
                endpoint.reset();
            }
        }
    };

  public:
    explicit Workspace(QObject& application) : ui_(std::make_shared<UiQueue>(application)) {}
    static std::shared_ptr<Workspace> Create(QObject& application, Json& launch) {
        auto self = std::make_shared<Workspace>(application);
        const auto& rdp = launch.at("rdp");
        if (launch.value("schema", 0) != 1 || rdp.value("schema", 0) != 1 || rdp.at("instance_id") != launch.at("instance_id") ||
            rdp.at("device_id") != launch.at("device_id")) {
            return {};
        }
        self->configuration_.account = rdp.at("account_name").get<std::string>();
        self->configuration_.domain = rdp.at("domain").get<std::string>();
        self->configuration_.proxy_certificate_sha256 = rdp.at("proxy_certificate_sha256").get<std::string>();
        const auto& password = rdp.at("password").get_ref<const std::string&>();
        self->configuration_.password = std::make_shared<SessionSecret>(std::span<const char>{password.data(), password.size()});
        self->configuration_.loopback_port = 1; // Only validation; replaced by the protected endpoint Ready callback before FreeRDP starts.
        self->configuration_.audio = launch.value("audio", true);
        self->configuration_.clipboard = launch.value("clipboard", true);
        if (!self->configuration_.IsValid()) {
            return {};
        }
        SdkConnectionParams params{};
        params.session_mode_ = SdkSessionMode::kRdp;
        params.ip_ = launch.at("host").get<std::string>();
        params.port_ = launch.at("port").get<int>();
        params.stream_id_ = launch.at("stream_id").get<std::string>();
        params.device_id_ = launch.at("visitor_id").get<std::string>();
        params.connection_ticket_ = launch.at("ticket").get<std::string>();
        params.connection_nonce_ = launch.at("nonce").get<std::string>();
        params.connection_instance_id_ = launch.at("instance_id").get<std::string>();
        if (params.ip_.empty() || params.ip_.size() > 253 || params.port_ <= 0 || params.port_ > 65535 || params.stream_id_.empty() ||
            params.stream_id_.size() > 128 || params.connection_ticket_.empty() || params.connection_ticket_.size() > 1024 ||
            params.connection_nonce_.empty() || params.connection_nonce_.size() > 128 || params.connection_instance_id_.empty()) {
            return {};
        }
        params.media_path_ = "/media?stream_id=" + UrlHelper::EncodeQueryComponent(params.stream_id_) +
                             "&remote_device_id=" + UrlHelper::EncodeQueryComponent(launch.at("device_id").get<std::string>()) +
                             "&visitor_device_id=" + UrlHelper::EncodeQueryComponent(params.device_id_);
        self->notifier_ = std::make_shared<MessageNotifier>();
        self->network_ = std::make_shared<NetClient>(std::move(params), self->notifier_);
        self->view_ = std::make_unique<RdpView>();
        self->timer_ = std::make_unique<QTimer>();
        self->ConnectCallbacks();
        return self;
    }
    ~Workspace() {
        Stop();
    }
    void Start() {
        view_->setWindowTitle(QStringLiteral("GammaRay · RDP 工作区 · 正在连接"));
        view_->resize(1280, 720);
        view_->show();
        timer_->start(1000);
        network_->Start();
    }
    void Stop() {
        if (stopping_.exchange(true)) {
            return;
        }
        ui_->Close();
        if (listener_) {
            listener_->UnListenAll();
            listener_.reset();
        }
        if (timer_) {
            timer_->stop();
        }
        if (view_) {
            view_->ReleaseInput();
        }
        if (session_) {
            session_->Stop();
            session_.reset();
        }
        configuration_.password.reset();
        endpoint_->Stop();
        if (network_) {
            network_->Exit();
        }
        if (notifier_) {
            notifier_->Stop();
        }
        if (own_file_clipboard_ && own_clipboard_ && QApplication::clipboard()->mimeData() == own_clipboard_.data()) {
            QApplication::clipboard()->clear(); // Cancel only this workspace's staging-file clipboard, never another application's clipboard.
        }
    }

  private:
    void Fail(QString reason) {
        if (stopping_.load()) {
            return;
        }
        if (view_) {
            view_->setWindowTitle(QStringLiteral("GammaRay · RDP · ") + reason);
        }
        Stop(); // Keep the explanatory window; no repeated modal connection/consent dialogs.
    }
    void ConnectCallbacks() {
        const auto weak = weak_from_this();
        const auto ui = ui_;
        listener_ = notifier_->CreateListener();
        listener_->Listen<SdkMsgWsConnectionRejected>([weak, ui](const SdkMsgWsConnectionRejected& event) {
            const auto rejection = event.rejection_;
            ui->Post([weak, rejection] {
                if (const auto self = weak.lock()) {
                    const auto reason = rejection == WsControlRejection::kOccupied        ? QStringLiteral("工作区已被占用，未抢占现有会话")
                                        : rejection == WsControlRejection::kAuthorization ? QStringLiteral("连接授权无效或已过期，请重新打开工作区")
                                                                                          : QStringLiteral("工作区连接被策略拒绝");
                    self->Fail(reason);
                }
            });
        });
        const auto executor = notifier_->GetAsyncRuntime()->Executor(PxAsyncLane::kWorker);
        const auto endpoint_slot = endpoint_;
        if (configuration_.clipboard) {
            QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged, view_.get(), [weak] {
                if (const auto self = weak.lock(); self && !self->stopping_.load()) {
                    self->PublishLocalClipboard();
                }
            });
        }
        // Network callback itself captures only routing capabilities, never a UI owner.
        const auto network = std::weak_ptr<NetClient>{network_};
        const auto opened = std::make_shared<std::atomic_bool>(false);
        network_->SetOnRdpMessageCallback([weak, ui, executor, network, opened, endpoint_slot](std::shared_ptr<Data> wire) {
            if (!opened->exchange(true)) {
                const auto binding = DecodeOpen(wire->Bytes());
                if (!binding) {
                    ui->Post([weak] {
                        if (const auto self = weak.lock()) {
                            self->Fail(QStringLiteral("无效 RDP 通道绑定"));
                        }
                    });
                    return;
                }
                const auto endpoint = RdpClientEndpoint::Create(
                    executor, *binding,
                    [network](std::shared_ptr<Data> bytes, RdpTcpBridge::SendCompletion completion) {
                        if (const auto client = network.lock()) {
                            client->PostRdpMessage(std::move(bytes), std::move(completion));
                        } else {
                            completion(false);
                        }
                    },
                    [weak, ui](std::uint16_t port) {
                        ui->Post([weak, port] {
                            if (const auto self = weak.lock()) {
                                self->StartSession(port);
                            }
                        });
                    },
                    [weak, ui](BridgeCloseReason) {
                        ui->Post([weak] {
                            if (const auto self = weak.lock()) {
                                self->Fail(QStringLiteral("RDP 通道已关闭"));
                            }
                        });
                    });
                endpoint_slot->Set(endpoint);
                if (!endpoint) {
                    ui->Post([weak] {
                        if (const auto self = weak.lock()) {
                            self->Fail(QStringLiteral("RDP 本地通道初始化失败"));
                        }
                    });
                }
            } else if (const auto endpoint = endpoint_slot->Get()) {
                static_cast<void>(endpoint->Receive(std::move(wire)));
            }
        });
        network_->SetOnDisconnectedCallback([weak, ui] {
            ui->Post([weak] {
                if (const auto self = weak.lock()) {
                    self->Fail(QStringLiteral("连接已断开，Windows 会话保留"));
                }
            });
        });
        QObject::connect(timer_.get(), &QTimer::timeout, view_.get(), [weak] {
            if (const auto self = weak.lock(); self && !self->stopping_.load()) {
                self->notifier_->SendAppMessage(SdkMsgTimer1000{});
                if (!self->first_frame_ && ++self->startup_seconds_ >= 25) {
                    self->Fail(QStringLiteral("等待远端首帧超时"));
                }
            }
        });
        QObject::connect(view_.get(), &RdpView::MouseInput, view_.get(), [weak](quint16 flags, int x, int y, bool extended) {
            if (const auto self = weak.lock(); self && self->session_) {
                self->session_->Mouse(flags, x, y, extended);
            }
        });
        QObject::connect(view_.get(), &RdpView::KeyInput, view_.get(), [weak](quint32 code, bool down) {
            if (const auto self = weak.lock(); self && self->session_) {
                self->session_->Key(code, down);
            }
        });
        QObject::connect(view_.get(), &RdpView::UnicodeInput, view_.get(), [weak](quint16 code, bool down) {
            if (const auto self = weak.lock(); self && self->session_) {
                self->session_->Unicode(code, down);
            }
        });
        QObject::connect(view_.get(), &RdpView::SynchronizeInput, view_.get(), [weak](quint16 toggles) {
            if (const auto self = weak.lock(); self && self->session_) {
                self->session_->Synchronize(toggles);
            }
        });
        QObject::connect(view_.get(), &RdpView::PauseInput, view_.get(), [weak] {
            if (const auto self = weak.lock(); self && self->session_) {
                self->session_->Pause();
            }
        });
        QObject::connect(view_.get(), &RdpView::ViewportChanged, view_.get(), [weak](int width, int height) {
            if (const auto self = weak.lock(); self && self->session_) {
                self->session_->Resize({width, height});
            }
        });
        QObject::connect(view_.get(), &RdpView::FrameConsumed, view_.get(), [weak](quint64 frame) {
            if (const auto self = weak.lock(); self && self->session_) {
                self->session_->ConsumeFrame(frame);
            }
        });
        QObject::connect(view_.get(), &RdpView::FramePresented, view_.get(), [weak](quint64, qint64, qint64 presented_us) {
            if (const auto self = weak.lock(); self && !self->stopping_.load()) {
                if (!self->first_frame_) {
                    self->first_frame_ = true;
                    self->view_->setWindowTitle(QStringLiteral("GammaRay · RDP 工作区"));
                    self->presentation_window_us_ = presented_us;
                }
                ++self->presented_frames_;
                const auto duration = presented_us - self->presentation_window_us_;
                if (duration >= 5000000) {
                    LOGI("event=rdp.presentation frames={} window_us={} fps={:.2f} viewport_width={} viewport_height={}", self->presented_frames_,
                         duration, self->presented_frames_ * 1000000.0 / duration, self->view_->ViewportPixels().width(),
                         self->view_->ViewportPixels().height());
                    self->presented_frames_ = 0;
                    self->presentation_window_us_ = presented_us;
                }
            }
        });
        QObject::connect(view_.get(), &RdpView::DisplayFailed, view_.get(), [weak](QString reason) {
            if (const auto self = weak.lock()) {
                self->Fail(std::move(reason));
            }
        });
        QObject::connect(view_.get(), &RdpView::DesktopRefreshRequested, view_.get(), [weak] {
            if (const auto self = weak.lock(); self && self->session_) {
                self->session_->Refresh();
            }
        });
    }
    void StartSession(std::uint16_t port) {
        if (stopping_.load() || session_) {
            return;
        }
        configuration_.loopback_port = port;
        configuration_.desktop = view_->ViewportPixels();
        const auto weak = weak_from_this();
        const auto ui = ui_;
        SessionCallbacks callbacks{};
        callbacks.frame = [weak, ui](std::shared_ptr<const DesktopFrame> frame) {
            const auto cost = static_cast<std::size_t>(frame->pixels.size());
            if (!ui->Post(
                    [weak, frame = std::move(frame)] {
                        if (const auto self = weak.lock(); self && !self->stopping_.load()) {
                            self->view_->ApplyFrame(frame);
                        }
                    },
                    cost)) {
                throw std::runtime_error("RDP UI frame queue capacity exceeded");
            }
        };
        callbacks.pointer = [weak, ui](PointerUpdate pointer) {
            const auto cost = static_cast<std::size_t>(pointer.image.sizeInBytes());
            if (!ui->Post(
                    [weak, pointer = std::move(pointer)] {
                        if (const auto self = weak.lock(); self && !self->stopping_.load()) {
                            switch (pointer.operation) {
                            case PointerOperation::kCreate:
                                self->view_->SetPointer(pointer.id, pointer.image, pointer.hotspot);
                                break;
                            case PointerOperation::kActivate:
                                self->view_->ActivatePointer(pointer.id);
                                break;
                            case PointerOperation::kRemove:
                                self->view_->RemovePointer(pointer.id);
                                break;
                            case PointerOperation::kDefault:
                                self->view_->SetDefaultPointer();
                                break;
                            case PointerOperation::kNull:
                                self->view_->SetNullPointer();
                                break;
                            }
                        }
                    },
                    cost)) {
                throw std::runtime_error("RDP UI pointer queue capacity exceeded");
            }
        };
        callbacks.phase = [weak, ui](SessionPhase phase, std::string reason) {
            ui->Post([weak, phase, reason = std::move(reason)] {
                if (const auto self = weak.lock(); self && !self->stopping_.load()) {
                    if (phase == SessionPhase::kFailed) {
                        self->Fail(QString::fromStdString(reason));
                    } else if (phase == SessionPhase::kDisconnected) {
                        self->Fail(QStringLiteral("Windows 会话已断开，未注销"));
                    }
                }
            });
        };
        callbacks.clipboard = [weak, ui](std::shared_ptr<const ClipboardData> data) {
            const auto cost = static_cast<std::size_t>(data->image.sizeInBytes() + (data->text ? data->text->size() * 2 : 0) +
                                                       (data->html ? data->html->size() * 2 : 0));
            if (!ui->Post(
                    [weak, data = std::move(data)] {
                        if (const auto self = weak.lock(); self && !self->stopping_.load()) {
                            self->ApplyRemoteClipboard(data);
                        }
                    },
                    cost)) {
                throw std::runtime_error("RDP UI clipboard queue capacity exceeded");
            }
        };
        session_ = RdpSession::Create(configuration_, std::move(callbacks));
        configuration_.password.reset();
        if (!session_) {
            Fail(QStringLiteral("RDP 协议初始化失败"));
        } else if (configuration_.clipboard) {
            PublishLocalClipboard();
        }
    }

    void PublishLocalClipboard() {
        if (!session_) {
            return;
        }
        const QPointer<const QMimeData> observed{QApplication::clipboard()->mimeData()}; // Qt-owned clipboard observation boundary.
        if (!observed || (own_clipboard_ && own_clipboard_ == observed) || applying_clipboard_) {
            return;
        }
        own_clipboard_.clear();
        own_file_clipboard_ = false;
        auto data = std::make_shared<ClipboardData>();
        if (observed->hasText()) {
            auto text = observed->text();
            if (text.size() <= kClipboardDataLimit / 2 - 1) {
                data->text = std::move(text);
            }
        }
        if (observed->hasHtml()) {
            auto html = observed->html();
            if (html.size() <= kClipboardDataLimit / 4) {
                data->html = std::move(html);
            }
        }
        if (observed->hasImage()) {
            auto image = qvariant_cast<QImage>(observed->imageData());
            if (image.sizeInBytes() <= kClipboardImageLimit) {
                data->image = std::move(image);
            }
        }
        if (observed->hasUrls()) {
            auto urls = observed->urls();
            if (urls.size() <= kClipboardEntryLimit) {
                data->urls = std::move(urls);
            }
        }
        session_->PublishClipboard(std::move(data));
    }
    void ApplyRemoteClipboard(const std::shared_ptr<const ClipboardData>& data) {
        applying_clipboard_ = true;
        // Ownership transfers directly to Qt at this annotated boundary; no
        // smart owner or parent is also responsible for deleting this object.
        QApplication::clipboard()->setMimeData(new ClipboardMimeData(data)); // NOLINT(gammaray-raw-pointer-boundary): Qt clipboard owner.
        own_clipboard_ = QApplication::clipboard()->mimeData();
        own_file_clipboard_ = !data->urls.isEmpty();
        applying_clipboard_ = false;
    }

    std::shared_ptr<UiQueue> ui_{};
    std::shared_ptr<MessageNotifier> notifier_{};
    std::shared_ptr<MessageListener> listener_{};
    std::shared_ptr<NetClient> network_{};
    std::shared_ptr<EndpointSlot> endpoint_{std::make_shared<EndpointSlot>()};
    std::unique_ptr<RdpView> view_{};
    std::unique_ptr<QTimer> timer_{};
    std::shared_ptr<RdpSession> session_{};
    SessionConfiguration configuration_{};
    std::atomic_bool stopping_{false};
    unsigned int startup_seconds_{0};
    bool first_frame_{false};
    std::uint64_t presented_frames_{0};
    qint64 presentation_window_us_{0};
    QPointer<const QMimeData> own_clipboard_{};
    bool applying_clipboard_{false};
    bool own_file_clipboard_{false};
};

} // namespace

int RunClient(QApplication& application) {
    try {
        if (!InitializeRdpRuntime()) {
            return 3;
        }
        auto secret = ReadLaunchPipe();
        if (!secret) {
            return 3;
        }
        auto launch = Json::parse(secret->View());
        secret.reset();
        auto workspace = Workspace::Create(application, launch);
        if (launch.contains("rdp") && launch.at("rdp").contains("password") && launch.at("rdp").at("password").is_string()) {
            auto& password = launch.at("rdp").at("password").get_ref<std::string&>();
            OPENSSL_cleanse(password.data(), password.size());
        }
        launch.clear();
        if (!workspace) {
            return 3;
        }
        workspace->Start();
        const auto result = application.exec();
        workspace->Stop();
        workspace.reset();
        return result;
    } catch (...) {
        // Never emit parser diagnostics, launch bodies or credentials to the log.
        return 3;
    }
}

} // namespace px::rdp
