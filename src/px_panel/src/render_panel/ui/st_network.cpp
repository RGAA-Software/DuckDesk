//
// Created by RGAA on 2024-06-10.
//

#include "st_network.h"
#include <QPointer>
#include "px_qt_widget/no_margin_layout.h"
#include "render_panel/px_context.h"
#include "render_panel/px_application.h"
#include "render_panel/px_settings.h"
#include "px_qt_widget/sized_msg_box.h"
#include "px_common_new/win32/dxgi_mon_detector.h"
#include "px_common_new/log.h"
#include "px_common_new/string_util.h"
#include "px_common_new/win32/audio_device_helper.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/companion/panel_companion.h"
#include "px_common_new/ip_util.h"
#include "px_dialog.h"
#include "px_label.h"
#include "px_pushbutton.h"
#include "st_network_search.h"
#include "px_console_client/console_device_api.h"
#include "px_console_client/console_device.h"
#include "px_console_client/console_http_client.h"
#include "px_relay_client/relay_api.h"
#include "px_common_new/message_notifier.h"
#include "render_panel/console_scanner/console_scanner.h"
#include "render_panel/console/console_error_presenter.h"
#include "st_network_auto_join_dialog.h"
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDebug>
#include <QFileDialog>
#include <chrono>
#include <optional>

#include "px_common_new/const_auto.h"
#include "px_common_new/async_blocking_call.h"
#include "px_common_new/async_runtime.h"
#include "px_common_new/latest_serial_request_gate.h"
#include "render_panel/devices/px_device_manager.h"
#include "render_panel/ui/qt_lifetime_guard.h"

namespace px {
namespace {
struct NetworkEndpointRequest final {
    std::string host;
    int console_port = 0;
    int relay_port = 0;
    std::string appkey;
};

struct ConsolePingResult final {
    Result<bool, px_console::ConsoleApiError> result;
    std::string server_message;
};

struct VerifyNetworkResult final {
    enum class Failure {
        kNone,
        kAsync,
        kConsole,
        kRelay,
    };

    Failure failure = Failure::kNone;
    PxAsyncError async_error;
    px_console::ConsoleApiError console_error = px_console::ConsoleApiError::kInternalError;
    std::string console_message;
    int relay_error = 0;
};

struct SaveNetworkResult final {
    enum class Failure {
        kNone,
        kAsync,
        kAuth,
        kDevice,
    };

    Failure failure = Failure::kNone;
    PxAsyncError async_error;
    px_console::ConsoleApiError device_error = px_console::ConsoleApiError::kInternalError;
    std::shared_ptr<px_console::ConsoleDevice> new_device;
};

template <typename T>
PxAwaitable<PxResult<std::optional<T>>> AwaitGatedBlockingCall(std::shared_ptr<LatestSerialRequestGate> gate,
                                                               LatestSerialRequestGate::Request request, PxBlockingTaskPoster poster,
                                                               std::chrono::steady_clock::time_point deadline, std::string stage,
                                                               std::function<T(const std::shared_ptr<std::atomic_bool>&)> call) {
    const auto executor = co_await asio::this_coro::executor;
    co_return co_await AwaitBlockingCall<std::optional<T>>(
        poster, executor, deadline, request.cancellation, std::move(stage),
        [gate, request, call = std::move(call)](const std::shared_ptr<std::atomic_bool>& cancellation) mutable {
            std::optional<T> result;
            static_cast<void>(gate->RunIfCurrent(request.generation, [&result, &call, &cancellation]() { result.emplace(call(cancellation)); }));
            return result;
        });
}

PxAwaitable<void> RunVerifyNetwork(std::shared_ptr<LatestSerialRequestGate> gate, LatestSerialRequestGate::Request request,
                                   PxBlockingTaskPoster poster, NetworkEndpointRequest endpoint, std::function<void(VerifyNetworkResult)> done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    auto console_call = co_await AwaitGatedBlockingCall<ConsolePingResult>(
        gate, request, poster, deadline, "panel-network.verify-console", [endpoint](const std::shared_ptr<std::atomic_bool>& cancellation) {
            auto result = px_console::ConsoleDeviceApi::Ping(endpoint.host, endpoint.console_port, endpoint.appkey, cancellation);
            return ConsolePingResult{
                .result = std::move(result),
                .server_message = px_console::ConsoleApiLastErrorMessage(),
            };
        });
    if (!console_call) {
        done(VerifyNetworkResult{
            .failure = VerifyNetworkResult::Failure::kAsync,
            .async_error = console_call.Error(),
        });
        co_return;
    }
    auto console_completion = console_call.TakeValue();
    if (!console_completion) {
        co_return;
    }
    if (!console_completion->result.has_value() || !console_completion->result.value()) {
        done(VerifyNetworkResult{
            .failure = VerifyNetworkResult::Failure::kConsole,
            .console_error =
                console_completion->result.has_value() ? px_console::ConsoleApiError::kServiceUnavailable : console_completion->result.error(),
            .console_message = std::move(console_completion->server_message),
        });
        co_return;
    }

    auto relay_call = co_await AwaitGatedBlockingCall<Result<bool, int>>(
        gate, request, poster, deadline, "panel-network.verify-relay", [endpoint](const std::shared_ptr<std::atomic_bool>& cancellation) {
            return px_relay::RelayApi::Ping(endpoint.host, endpoint.relay_port, endpoint.appkey, cancellation);
        });
    if (!relay_call) {
        done(VerifyNetworkResult{
            .failure = VerifyNetworkResult::Failure::kAsync,
            .async_error = relay_call.Error(),
        });
        co_return;
    }
    auto relay_completion = relay_call.TakeValue();
    if (!relay_completion) {
        co_return;
    }
    if (!relay_completion->has_value() || !relay_completion->value()) {
        done(VerifyNetworkResult{
            .failure = VerifyNetworkResult::Failure::kRelay,
            .relay_error = relay_completion->has_value() ? 0 : relay_completion->error(),
        });
        co_return;
    }
    done({});
    co_return;
}

PxAwaitable<void> RunSaveNetwork(std::shared_ptr<LatestSerialRequestGate> gate, LatestSerialRequestGate::Request request, PxBlockingTaskPoster poster,
                                 std::function<std::shared_ptr<Authorization>(const std::shared_ptr<std::atomic_bool>&)> request_auth,
                                 std::shared_ptr<PxDeviceManager> device_manager, std::string device_id, std::string default_device_name,
                                 std::function<void(SaveNetworkResult)> done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    if (request_auth) {
        auto auth_call = co_await AwaitGatedBlockingCall<std::shared_ptr<Authorization>>(
            gate, request, poster, deadline, "panel-network.request-auth",
            [request_auth](const std::shared_ptr<std::atomic_bool>& cancellation) { return request_auth(cancellation); });
        if (!auth_call) {
            done(SaveNetworkResult{
                .failure = SaveNetworkResult::Failure::kAsync,
                .async_error = auth_call.Error(),
            });
            co_return;
        }
        auto auth = auth_call.TakeValue();
        if (!auth) {
            co_return;
        }
        if (!*auth) {
            done(SaveNetworkResult{
                .failure = SaveNetworkResult::Failure::kAuth,
            });
            co_return;
        }
    }

    bool request_new_device = device_id.empty();
    if (!request_new_device) {
        auto query_call = co_await AwaitGatedBlockingCall<Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>>(
            gate, request, poster, deadline, "panel-network.query-device",
            [device_manager, device_id](const std::shared_ptr<std::atomic_bool>& cancellation) {
                return device_manager->QueryDevice(device_id, cancellation);
            });
        if (!query_call) {
            done(SaveNetworkResult{
                .failure = SaveNetworkResult::Failure::kAsync,
                .async_error = query_call.Error(),
            });
            co_return;
        }
        auto query = query_call.TakeValue();
        if (!query) {
            co_return;
        }
        request_new_device = !query->has_value() || !query->value() || query->value()->device_id_.empty();
    }

    if (!request_new_device) {
        done({});
        co_return;
    }
    auto create_call = co_await AwaitGatedBlockingCall<Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>>(
        gate, request, poster, deadline, "panel-network.create-device",
        [device_manager, default_device_name](const std::shared_ptr<std::atomic_bool>& cancellation) {
            return device_manager->RequestNewDevice(default_device_name, "", cancellation);
        });
    if (!create_call) {
        done(SaveNetworkResult{
            .failure = SaveNetworkResult::Failure::kAsync,
            .async_error = create_call.Error(),
        });
        co_return;
    }
    auto created = create_call.TakeValue();
    if (!created) {
        co_return;
    }
    if (!created->has_value() || !created->value() || created->value()->device_id_.empty() || created->value()->gen_random_pwd_.empty()) {
        done(SaveNetworkResult{
            .failure = SaveNetworkResult::Failure::kDevice,
            .device_error = created->has_value() ? px_console::ConsoleApiError::kInternalError : created->error(),
        });
        co_return;
    }
    done(SaveNetworkResult{
        .new_device = created->value(),
    });
    co_return;
}
} // namespace

StNetwork::StNetwork(const std::shared_ptr<PxApplication>& app,
                     QWidget* parent) // NOLINT(gammaray-raw-pointer-boundary) Qt parent API
    : TabBase(app, parent), network_settings_(*PxSettings::Instance()), verify_gate_(LatestSerialRequestGate::Create()),
      save_gate_(LatestSerialRequestGate::Create()) {
    if (const auto notifier = context_->GetMessageNotifier()) {
        request_scope_ = PxAsyncScope::Create(notifier->GetAsyncRuntime(), PxAsyncLane::kControl);
    }
    const QPointer<StNetwork> self(this);
    auto root_layout = new NoMarginHLayout();
    auto column1_layout = new NoMarginVLayout();
    root_layout->addLayout(column1_layout);

    auto column2_layout = new NoMarginVLayout();
    root_layout->addSpacing(10);
    root_layout->addLayout(column2_layout);

    root_layout->addStretch();

    // segment encoder
    auto tips_label_width = 300;
    auto tips_label_height = 35;
    auto tips_label_size = QSize(tips_label_width, tips_label_height);
    auto input_size = QSize(280, tips_label_height);

    {
        auto segment_layout = new NoMarginVLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
        // Servers
        {
            // title
            auto label = new TcLabel(this); // NOLINT(gammaray-raw-pointer-boundary): Qt parent owns the widget
            label->SetTextId("id_gr_console_server");
            label->setStyleSheet("font-size: 16px; font-weight: 700;");
            segment_layout->addSpacing(0);
            segment_layout->addWidget(label);
            segment_layout->addSpacing(2);
        }

        // Console access info
        {
            auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
            auto label = new TcLabel(this);
            label->SetTextId("id_console_auth_access_info");
            label->setFixedSize(tips_label_size);
            label->setStyleSheet("font-size: 14px; font-weight: 500;");
            layout->addWidget(label);

            auto edit = new QTextEdit(this); // NOLINT(gammaray-raw-pointer-boundary): Qt parent owns the widget
            edit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            edit->setLineWrapMode(QTextEdit::WidgetWidth);
            edit->setAcceptRichText(false);
            edt_console_access_ = edit;
            QObject::connect(edit, &QTextEdit::textChanged, this, MakeQtLifetimeAction(self, [](const QPointer<StNetwork>& page) {
                                 if (!page->edt_console_access_) {
                                     return;
                                 }
                                 const auto text = page->edt_console_access_->toPlainText();
                                 page->DisplayConsoleAccessInfo(page->ParseConsoleAccessInfo(text.toStdString()));
                             }));
            edit->setFixedSize(input_size.width() * 2, input_size.height() * 2);
            edit->setText(network_settings_.get().GetConsoleAccessInfo().c_str());
            layout->addWidget(edit);
            layout->addSpacing(15);

            {
                auto search_layout = new NoMarginVLayout();

                auto search = new TcPushButton();
                search->SetTextId("id_file_trans_search");
                search->setFixedSize(80, 32);
                search_layout->addWidget(search);
                connect(search, &QPushButton::clicked, this,
                        MakeQtLifetimeAction(self, [](const QPointer<StNetwork>& page) { page->SearchAccessInfo(false); }));

                search_layout->addSpacing(5);

                auto verify = new TcPushButton();
                verify->SetTextId("id_verify");
                verify->setFixedSize(80, 32);
                search_layout->addWidget(verify);
                connect(verify, &QPushButton::clicked, this,
                        MakeQtLifetimeAction(self, [](const QPointer<StNetwork>& page) { page->VerifyAccessInfo(); }));

                search_layout->addStretch();
                layout->addLayout(search_layout);
            }

            layout->addStretch();

            segment_layout->addSpacing(5);
            segment_layout->addLayout(layout);
        }

        // Manager Server
        {
            auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
            auto label = new TcLabel(this);
            label->SetTextId("id_server_host");
            label->setFixedSize(tips_label_size);
            label->setStyleSheet("font-size: 14px; font-weight: 500;");
            layout->addWidget(label);

            auto edit = new QLineEdit(this);
            edit->setEnabled(false);
            edt_console_server_host_ = edit;
            edit->setFixedSize(input_size);
            edit->setText(network_settings_.get().GetConsoleServerHost().c_str());
            layout->addWidget(edit);
            layout->addStretch();
            segment_layout->addSpacing(5);
            segment_layout->addLayout(layout);
        }
        {
            auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
            auto label = new TcLabel(this);
            label->SetTextId("id_server_port");
            label->setFixedSize(tips_label_size);
            label->setStyleSheet("font-size: 14px; font-weight: 500;");
            layout->addWidget(label);

            auto edit = new QLineEdit(this);
            edit->setEnabled(false);
            edt_console_server_port_ = edit;
            edit->setFixedSize(input_size);
            edit->setValidator(new QIntValidator);
            edit->setText(std::to_string(network_settings_.get().GetConsoleServerPort()).c_str());
            layout->addWidget(edit);
            layout->addStretch();
            segment_layout->addSpacing(5);
            segment_layout->addLayout(layout);
        }

        // Relay Server
        {
            auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
            auto label = new TcLabel(this);
            label->SetTextId("id_relay_host");
            label->setFixedSize(tips_label_size);
            label->setStyleSheet("font-size: 14px; font-weight: 500;");
            layout->addWidget(label);

            auto edit = new QLineEdit(this);
            edit->setEnabled(false);
            edt_relay_server_host_ = edit;
            edit->setFixedSize(input_size);
            edit->setText(network_settings_.get().GetRelayServerHost().c_str());
            layout->addWidget(edit);
            layout->addStretch();
            segment_layout->addSpacing(5);
            segment_layout->addLayout(layout);
        }
        {
            auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
            auto label = new TcLabel(this);
            label->SetTextId("id_relay_port");
            label->setFixedSize(tips_label_size);
            label->setStyleSheet("font-size: 14px; font-weight: 500;");
            layout->addWidget(label);

            auto edit = new QLineEdit(this);
            edit->setEnabled(false);
            edt_relay_server_port_ = edit;
            edit->setFixedSize(input_size);
            edit->setValidator(new QIntValidator);
            edit->setText(std::to_string(network_settings_.get().GetRelayServerPort()).c_str());
            layout->addWidget(edit);
            layout->addStretch();
            segment_layout->addSpacing(5);
            segment_layout->addLayout(layout);
        }

        // PORT settings
        {
            // title
            auto label = new TcLabel(this);
            label->SetTextId("id_network_settings");
            label->setStyleSheet("font-size: 16px; font-weight: 700;");
            segment_layout->addSpacing(20);
            segment_layout->addWidget(label);
            segment_layout->addSpacing(2);
        }
        // Network type
        {
            auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
            auto label = new TcLabel(this);
            label->SetTextId("id_websocket");
            label->setFixedSize(tips_label_size);
            label->setStyleSheet("font-size: 14px; font-weight: 500;");
            layout->addWidget(label);

            auto edit = new QCheckBox(this);
            cb_websocket_ = edit;
            edit->setFixedSize(input_size);
            layout->addWidget(edit);
            layout->addStretch();
            segment_layout->addSpacing(5);
            segment_layout->addLayout(layout);
            edit->setChecked(network_settings_.get().IsWebSocketEnabled());
            connect(edit, &QCheckBox::checkStateChanged, this, [self](Qt::CheckState state) {
                if (state == Qt::CheckState::Checked || !self) {
                    return;
                }
                const auto context = self->context_;
                context->PostUIDelayTask(
                    [self]() {
                        if (!self) {
                            return;
                        }
                        TcDialog dialog(tcTr("id_tips"), tcTr("id_dialog_ssl_streaming_always_on"));
                        dialog.exec();
                        self->network_settings_.get().SetWebSocketEnabled(true);
                        if (self->cb_websocket_) {
                            self->cb_websocket_->setChecked(true);
                        }
                    },
                    50);
            });
        }
        // Streaming WebSocket port
        {
            auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
            auto label = new TcLabel(this);
            label->SetTextId("id_streaming_websocket_port");
            label->setFixedSize(tips_label_size);
            label->setStyleSheet("font-size: 14px; font-weight: 500;");
            layout->addWidget(label);

            auto edit = new QLineEdit(this);
            edt_websocket_ = edit;
            edit->setFixedSize(input_size);
            edit->setText(std::to_string(network_settings_.get().GetRenderServerPort()).c_str());
            edit->setEnabled(network_settings_.get().IsWebSocketEnabled());
            layout->addWidget(edit);
            layout->addStretch();
            segment_layout->addSpacing(5);
            segment_layout->addLayout(layout);
        }
        {
            auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
            auto label = new TcLabel(this);
            label->SetTextId("id_udp");
            label->setFixedSize(tips_label_size);
            label->setStyleSheet("font-size: 14px; font-weight: 500;");
            layout->addWidget(label);

            auto edit = new QCheckBox(this);
            cb_udp_kcp_ = edit;
            edit->setFixedSize(input_size);
            edit->setEnabled(true);
            layout->addWidget(edit);
            layout->addStretch();
            segment_layout->addSpacing(5);
            segment_layout->addLayout(layout);
            edit->setChecked(network_settings_.get().udp_kcp_enabled_ == kStTrue);
            connect(edit, &QCheckBox::stateChanged, this, [self](int state) {
                if (!self) {
                    return;
                }
                const bool enabled = state == 2;
                self->network_settings_.get().SetUdpKcpEnabled(enabled);
                if (self->edt_udp_kcp_) {
                    self->edt_udp_kcp_->setEnabled(enabled);
                }
            });
        }
        // UdpKcp port
        {
            auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
            auto label = new TcLabel(this);
            label->SetTextId("id_streaming_udp_port");
            label->setFixedSize(tips_label_size);
            label->setStyleSheet("font-size: 14px; font-weight: 500;");
            layout->addWidget(label);

            auto edit = new QLineEdit(this);
            edt_udp_kcp_ = edit;
            edit->setFixedSize(input_size);
            edit->setText(std::to_string(network_settings_.get().udp_listen_port_).c_str());
            edit->setEnabled(network_settings_.get().udp_kcp_enabled_ == kStTrue);
            layout->addWidget(edit);
            layout->addStretch();
            segment_layout->addSpacing(5);
            segment_layout->addLayout(layout);
        }
        {
            auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
            auto label = new TcLabel(this);
            label->SetTextId("id_rtc");
            label->setFixedSize(tips_label_size);
            label->setStyleSheet("font-size: 14px; font-weight: 500;");
            layout->addWidget(label);

            auto edit = new QCheckBox(this);
            cb_webrtc_ = edit;
            edit->setFixedSize(input_size);
            edit->setEnabled(true);
            layout->addWidget(edit);
            layout->addStretch();
            segment_layout->addSpacing(5);
            segment_layout->addLayout(layout);
            edit->setChecked(network_settings_.get().webrtc_enabled_ == kStTrue);
            connect(edit, &QCheckBox::stateChanged, this, [self](int state) {
                if (self) {
                    self->network_settings_.get().SetWebRTCEnabled(state == 2);
                }
            });
        }
        // Ethernet adapter
        {
            auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
            auto label = new TcLabel(this);
            label->SetTextId("id_ethernet_adapter");
            label->setFixedSize(tips_label_size);
            label->setStyleSheet("font-size: 14px; font-weight: 500;");
            layout->addWidget(label);

            auto edit = new QComboBox(this);
            edit->setFixedSize(input_size);
            edit->addItem("Auto");
            auto all_et_info = context_->GetIps();
            int index = 0;
            int target_index_ = -1;
            for (const auto& et_info : all_et_info) {
                if (et_info.ip_addr_ == network_settings_.get().network_listening_ip_ && !et_info.ip_addr_.empty()) {
                    target_index_ = index;
                }
                edit->addItem(std::format("{} {} {}", et_info.ip_addr_, (et_info.nt_type_ == IPNetworkType::kWired ? "WIRED" : "WIRELESS"),
                                          et_info.human_readable_name_)
                                  .c_str());
                index++;
            }
            if (target_index_ != -1) {
                edit->setCurrentIndex(target_index_ + 1);
            }
            connect(edit, &QComboBox::currentIndexChanged, this, [self, all_et_info](int idx) {
                if (!self) {
                    return;
                }
                if (idx <= 0) {
                    self->network_settings_.get().SetListeningIp("");
                    return;
                }
                auto target_ip = all_et_info.at(idx - 1).ip_addr_;
                self->network_settings_.get().SetListeningIp(target_ip);
            });
            layout->addWidget(edit);
            layout->addStretch();
            segment_layout->addSpacing(5);
            segment_layout->addLayout(layout);
        }
        // Panel listening port
        {
            auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
            auto label = new TcLabel(this);
            label->SetTextId("id_panel_listening_port");
            label->setFixedSize(tips_label_size);
            label->setStyleSheet("font-size: 14px; font-weight: 500;");
            layout->addWidget(label);

            auto edit = new QLineEdit(this);
            edt_panel_port_ = edit;
            edit->setFixedSize(input_size);
            edit->setText(std::to_string(network_settings_.get().GetPanelServerPort()).c_str());
            edit->setEnabled(true);
            layout->addWidget(edit);
            layout->addStretch();
            segment_layout->addSpacing(5);
            segment_layout->addLayout(layout);
        }

        column1_layout->addLayout(segment_layout);
    }

    {
        auto layout = new NoMarginHLayout(); // NOLINT(gammaray-raw-pointer-boundary): transient Qt ownership handoff
        auto btn = new TcPushButton(this);
        btn->SetTextId("id_save");
        btn->setFixedSize(QSize(220, 35));
        btn->setStyleSheet("font-size: 14px; font-weight: 700;");
        layout->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, MakeQtLifetimeAction(self, [](const QPointer<StNetwork>& page) { page->Save(false); }));

        layout->addStretch();
        column1_layout->addSpacing(30);
        column1_layout->addLayout(layout);
    }

    column1_layout->addStretch();

    setLayout(root_layout);

    // messages
    msg_listener_ = context_->ObtainUIMessageListener();
    msg_listener_->Listen<MsgForceClearProgramData>([self](const MsgForceClearProgramData&) {
        if (!self) {
            return;
        }
        self->edt_console_access_->setText("");
        self->edt_console_server_host_->setText("");
        self->edt_console_server_port_->setText("");
        self->edt_relay_server_host_->setText("");
        self->edt_relay_server_port_->setText("");
    });

    //
    context_->PostUIDelayTask(
        [self]() {
            if (self) {
                self->SearchAccessInfo(true);
            }
        },
        5000);
}

StNetwork::~StNetwork() {
    verify_gate_->Stop();
    save_gate_->Stop();
    if (request_scope_) {
        static_cast<void>(request_scope_->StopAndWait(std::chrono::seconds(2)));
    }
}

void StNetwork::OnTabShow() {}

void StNetwork::OnTabHide() {}

std::shared_ptr<ConsoleAccessInfo> StNetwork::ParseConsoleAccessInfo(const std::string& info) {
    return app_->GetCompanion() ? app_->GetCompanion()->ParseConsoleAccessInfo(info) : nullptr;
}

void StNetwork::DisplayConsoleAccessInfo(const std::shared_ptr<ConsoleAccessInfo>& info) {
    if (!info || !info->console_config_.IsValid()) {
        if (edt_console_server_host_) {
            edt_console_server_host_->setText("");
        }
        if (edt_console_server_port_) {
            edt_console_server_port_->setText("");
        }
        if (edt_relay_server_host_) {
            edt_relay_server_host_->setText("");
        }
        if (edt_relay_server_port_) {
            edt_relay_server_port_->setText("");
        }
        return;
    }
    if (edt_console_server_host_) {
        edt_console_server_host_->setText(info->console_config_.srv_w3c_ip_.c_str());
    }
    if (edt_console_server_port_) {
        edt_console_server_port_->setText(QString::number(info->console_config_.srv_console_port_));
    }
    if (edt_relay_server_host_) {
        edt_relay_server_host_->setText(info->console_config_.srv_w3c_ip_.c_str());
    }
    if (edt_relay_server_port_) {
        edt_relay_server_port_->setText(QString::number(info->console_config_.srv_relay_port_));
    }
}

void StNetwork::SaveConsoleAccessInfo() {
    auto info = edt_console_access_->toPlainText().trimmed().toStdString();
    network_settings_.get().SetConsoleAccessInfo(info);
}

void StNetwork::SearchAccessInfo(bool auto_restart_render) {
    auto ac_info = app_->GetConsoleScanner()->GetConsoleAccessInfo();
    if (ac_info.empty()) {
        return;
    }
    if (ac_info.size() == 1) {
        std::shared_ptr<StNetworkConsoleAccessInfo> info = nullptr;
        for (const auto& [k, v] : ac_info) {
            info = v;
        }
        if (info) {
            auto& settings = network_settings_.get();
            if (settings.GetConsoleServerHost() != info->console_ip_ || settings.GetConsoleServerPort() != info->console_port_ ||
                settings.GetRelayServerHost() != info->relay_ip_ || settings.GetRelayServerPort() != info->relay_port_ || !auto_restart_render) {
                StNetworkAutoJoinDialog dialog(app_, info);
                if (dialog.exec() == 0) {
                    edt_console_access_->setText(info->origin_info_.c_str());
                    if (auto_restart_render) {
                        Save(auto_restart_render);
                    }
                }
            }
        }
    } else {
        StNetworkSearch nt_search(app_, this);
        if (nt_search.exec() == 0) {
            auto selected_item = nt_search.GetSelectedItem();
            if (!selected_item) {
                LOGE("Not a valid console item !");
                return;
            }
            if (selected_item->console_ip_.empty() || selected_item->relay_ip_.empty()) {
                TcDialog dialog(tcTr("id_error"), tcTr("id_console_access_info_invalid"));
                dialog.exec();
                return;
            }
            edt_console_access_->setText(selected_item->origin_info_.c_str());
        }
    }
}

void StNetwork::VerifyAccessInfo() {
    const auto ac_info = ParseConsoleAccessInfo(edt_console_access_->toPlainText().trimmed().toStdString());
    if (!ac_info) {
        LOGE("Parse access info failed: {}", edt_console_access_->toPlainText().toStdString());
        TcDialog dialog(tcTr("id_error"), tcTr("id_console_access_info_invalid"));
        dialog.exec();
        return;
    }
    if (!request_scope_) {
        return;
    }
    px_console::SetConsoleSslEnabled(true);
    const auto request = verify_gate_->Begin();
    if (!request) {
        return;
    }
    const NetworkEndpointRequest endpoint{
        .host = ac_info->console_config_.srv_w3c_ip_,
        .console_port = ac_info->console_config_.srv_console_port_,
        .relay_port = ac_info->console_config_.srv_relay_port_,
        .appkey = ac_info->console_config_.srv_appkey_,
    };
    const auto context = context_;
    const auto gate = verify_gate_;
    const QPointer<StNetwork> self(this);
    const PxBlockingTaskPoster poster = [context](std::function<void()> task) { context->PostNetworkTask(std::move(task)); };
    const bool spawned = request_scope_->Spawn("panel-network-verify", [gate, request, poster, endpoint, context, self]() {
        return RunVerifyNetwork(
            gate, request, poster, endpoint, [gate, generation = request.generation, endpoint, context, self](VerifyNetworkResult result) {
                context->PostUITask([gate, generation, endpoint, self, result = std::move(result)]() {
                    if (!self || !gate->Complete(generation)) {
                        return;
                    }
                    if (result.failure == VerifyNetworkResult::Failure::kAsync) {
                        LOGE("Verify network failed: stage={}, reason={}", result.async_error.stage, result.async_error.message);
                        if (result.async_error.code == PxAsyncErrorCode::kCancelled) {
                            return;
                        }
                        TcDialog dialog(tcTr("id_error"),
                                        MakeConsoleErrorMessage(ConsoleErrorOperation::kCheckConsole,
                                                                px_console::ConsoleApiError::kNetworkUnavailable, result.async_error.message,
                                                                MakeConsoleEndpoint(endpoint.host, endpoint.console_port)),
                                        self);
                        dialog.exec();
                        return;
                    }
                    if (result.failure == VerifyNetworkResult::Failure::kConsole) {
                        TcDialog dialog(tcTr("id_error"),
                                        MakeConsoleErrorMessage(ConsoleErrorOperation::kCheckConsole, result.console_error, result.console_message,
                                                                MakeConsoleEndpoint(endpoint.host, endpoint.console_port)),
                                        self);
                        dialog.exec();
                        return;
                    }
                    if (result.failure == VerifyNetworkResult::Failure::kRelay) {
                        const auto relay_endpoint =
                            QStringLiteral("http://%1:%2").arg(QString::fromStdString(endpoint.host)).arg(endpoint.relay_port);
                        const auto message = tcTr("id_verify_relay_failed_actionable") + "\n" + tcTr("id_error_endpoint_label") + ": " +
                                             relay_endpoint + "\n" + tcTr("id_error_code_label") + ": RELAY-" + QString::number(result.relay_error);
                        TcDialog dialog(tcTr("id_error"), message, self);
                        dialog.exec();
                        return;
                    }
                    TcDialog dialog(tcTr("id_tips"), tcTr("id_verify_success"), self);
                    dialog.exec();
                });
            });
    });
    if (!spawned) {
        request.cancellation->store(true, std::memory_order_release);
        static_cast<void>(gate->Complete(request.generation));
    }
}

void StNetwork::Save(bool auto_restart_render) {
    if (!request_scope_) {
        return;
    }
    const auto console_host = edt_console_server_host_->text().toStdString();
    const auto console_port = edt_console_server_port_->text().toStdString();
    const auto relay_host = edt_relay_server_host_->text().toStdString();
    const auto relay_port = edt_relay_server_port_->text().toStdString();
    auto& settings = network_settings_.get();
    bool force_update_device_id = false;
    if (!console_host.empty() &&
        (settings.GetConsoleServerHost() != console_host || settings.GetConsoleServerPort() != std::atoi(console_port.c_str()))) {
        force_update_device_id = true;
        settings.SetDeviceId("");
        if (app_->GetCompanion()) {
            app_->GetCompanion()->UpdateDeviceId("");
        }
        settings.SetDeviceName("");
        settings.SetDeviceRandomPwd("");
        LOGW("Clear old device id, force updating device id.");
    }
    settings.SetConsoleServerHost(console_host);
    settings.SetConsoleServerPort(console_port);
    settings.SetPanelServerPort(edt_panel_port_->text().toInt());

    settings.SetRelayServerHost(relay_host);
    settings.SetRelayServerPort(relay_port);

    SaveConsoleAccessInfo();
    settings.SetConsoleSslEnabled(true);
    settings.Load();

    // companion
    std::function<std::shared_ptr<Authorization>(const std::shared_ptr<std::atomic_bool>&)> request_auth;
    if (app_->GetCompanion()) {
        app_->GetCompanion()->UpdateConsoleServerConfig(settings.GetConsoleServerHost(), settings.GetConsoleServerPort(),
                                                        settings.IsConsoleSslEnabled());

        const auto ac_info = ParseConsoleAccessInfo(edt_console_access_->toPlainText().trimmed().toStdString());
        if (ac_info && !ac_info->console_config_.srv_appkey_.empty()) {
            app_->GetCompanion()->UpdateAppkey(ac_info->console_config_.srv_appkey_);
        }
        request_auth = [application = app_](const std::shared_ptr<std::atomic_bool>&) {
            return application->GetCompanion() ? application->GetCompanion()->RequestAuth() : nullptr;
        };
    }

    app_->RefreshClientManagerSettings();
    std::string default_device_name = "D-NULL";
    const auto ips = context_->GetIps();
    if (!ips.empty()) {
        std::vector<std::string> segments;
        StringUtil::Split(ips.front().ip_addr_, segments, ".");
        if (!segments.empty()) {
            default_device_name = "D-" + segments.back();
        }
    }
    const auto request = save_gate_->Begin();
    if (!request) {
        return;
    }
    const auto device_manager = app_->GetDeviceManager();
    const auto context = context_;
    const auto gate = save_gate_;
    const auto device_id = settings.GetDeviceId();
    const QPointer<StNetwork> self(this);
    const PxBlockingTaskPoster poster = [context](std::function<void()> task) { context->PostNetworkTask(std::move(task)); };
    const bool spawned = request_scope_->Spawn("panel-network-save", [gate, request, poster, request_auth, device_manager, device_id,
                                                                      default_device_name = std::move(default_device_name), context, self,
                                                                      force_update_device_id, auto_restart_render]() mutable {
        return RunSaveNetwork(
            gate, request, poster, request_auth, device_manager, std::move(device_id), std::move(default_device_name),
            [gate, generation = request.generation, context, self, force_update_device_id, auto_restart_render](SaveNetworkResult result) mutable {
                context->PostUITask([gate, generation, self, force_update_device_id, auto_restart_render, result = std::move(result)]() mutable {
                    if (!self || !gate->Complete(generation)) {
                        return;
                    }
                    if (result.failure == SaveNetworkResult::Failure::kAsync) {
                        LOGE("Save network failed: stage={}, reason={}", result.async_error.stage, result.async_error.message);
                        if (result.async_error.code != PxAsyncErrorCode::kCancelled) {
                            TcDialog dialog(tcTr("id_warning"), QString::fromStdString(result.async_error.message), self);
                            dialog.exec();
                        }
                        return;
                    }
                    if (result.failure == SaveNetworkResult::Failure::kAuth) {
                        TcDialog dialog(tcTr("id_warning"), tcTr("id_cant_request_auth"), self);
                        dialog.exec();
                        return;
                    }
                    if (result.failure == SaveNetworkResult::Failure::kDevice) {
                        LOGE("Request Device ID failed, code: {}", static_cast<int>(result.device_error));
                        TcDialog dialog(tcTr("id_warning"), tcTr("id_request_device_id_failed"), self);
                        dialog.exec();
                        return;
                    }
                    if (result.new_device) {
                        auto& settings = self->network_settings_.get();
                        settings.SetDeviceId(result.new_device->device_id_);
                        settings.SetDeviceName(result.new_device->device_name_);
                        settings.SetDeviceRandomPwd(result.new_device->gen_random_pwd_);
                        if (self->app_->GetCompanion()) {
                            self->app_->GetCompanion()->UpdateDeviceId(result.new_device->device_id_);
                        }
                        self->context_->SendAppMessage(MsgRequestedNewDevice{
                            .device_id_ = result.new_device->device_id_,
                            .device_random_pwd_ = result.new_device->gen_random_pwd_,
                            .force_update_ = true,
                        });
                        self->context_->SendAppMessage(MsgSyncSettingsToRender{});
                    }
                    self->context_->SendAppMessage(MsgSettingsChanged{
                        .settings_ = PxSettings::Instance(),
                        .force_update_device_id_ = force_update_device_id,
                    });
                    if (auto_restart_render) {
                        self->context_->SendAppMessage(AppMsgRestartServer{});
                        return;
                    }
                    TcDialog dialog(tcTr("id_tips"), tcTr("id_save_settings_restart_renderer"), self);
                    if (dialog.exec() == kDoneOk) {
                        self->context_->SendAppMessage(AppMsgRestartServer{});
                    }
                });
            });
    });
    if (!spawned) {
        request.cancellation->store(true, std::memory_order_release);
        static_cast<void>(gate->Complete(request.generation));
    }
}

} // namespace px
