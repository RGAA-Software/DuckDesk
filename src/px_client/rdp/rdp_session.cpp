#include "rdp_session.h"
#include "rdp_clipboard_channel.h"
#include "rdp_display_channel.h"
#include "rdp_process_audio_controller.h"

#include "px_common/async_runtime.h"
#include "px_common/log.h"
#include "px_common/win32/unique_win_handle.h"
#include "px_rdp/rdp_proxy_policy.h"
#include <freerdp/client.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/disp.h>
#include <freerdp/channels/channels.h>
#include <freerdp/codec/color.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/graphics.h>
#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <freerdp/update.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>

namespace px::rdp {
namespace {

bool SafeName(std::string_view value) {
    return !value.empty() && std::ranges::all_of(value, [](unsigned char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_' || byte == '-';
    });
}

bool ValidSize(const Size size) {
    return size.width >= 200 && size.height >= 200 && size.width <= 8192 && size.height <= 8192 &&
           static_cast<std::int64_t>(size.width) * size.height * 4 <= static_cast<std::int64_t>(kMaximumFrameBytes);
}

Rectangle Unite(const Rectangle left, const Rectangle right) {
    if (left.Empty()) return right;
    if (right.Empty()) return left;
    const int x = std::min(left.x, right.x);
    const int y = std::min(left.y, right.y);
    const int rightEdge = std::max(left.x + left.width, right.x + right.width);
    const int bottom = std::max(left.y + left.height, right.y + right.height);
    return {x, y, rightEdge - x, bottom - y};
}

Rectangle Intersect(const Rectangle left, const Rectangle right) {
    const int x = std::max(left.x, right.x);
    const int y = std::max(left.y, right.y);
    const int rightEdge = std::min(left.x + left.width, right.x + right.width);
    const int bottom = std::min(left.y + left.height, right.y + right.height);
    return rightEdge <= x || bottom <= y ? Rectangle{} : Rectangle{x, y, rightEdge - x, bottom - y};
}

template <typename Function> BOOL Guard(Function function) noexcept {
    try {
        return function() ? TRUE : FALSE;
    } catch (...) {
        return FALSE;
    }
}

struct ContextCloser final {
    void operator()(rdpContext* context) const noexcept { // NOLINT(pixels-raw-pointer-boundary): owned FreeRDP allocation deleter ABI.
        if (context) {
            if (context->gdi) {
                gdi_free(context->instance);
            }
            freerdp_client_context_free(context);
        }
    }
};

enum class CommandKind { kMouse, kKey, kUnicode, kSynchronize, kPause, kResize, kFrameConsumed, kRefresh };
struct Command final {
    CommandKind kind{CommandKind::kRefresh};
    std::uint64_t value{0};
    int x{0};
    int y{0};
    bool flag{false};
};

} // namespace

bool InitializeRdpRuntime() {
    // OpenSSL's compiled-in provider path belongs to the SDK build host.
    std::array<wchar_t, 32768> executable{};
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        return false;
    }
    const auto directory = std::filesystem::path{std::wstring_view{executable.data(), length}}.parent_path();
    const auto provider = directory / "legacy.dll";
    const auto attributes = GetFileAttributesW(provider.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
           _wputenv_s(L"OPENSSL_MODULES", directory.c_str()) == 0 && SetEnvironmentVariableW(L"OPENSSL_MODULES", directory.c_str()) &&
           SetEnvironmentVariableW(L"WINPR_NATIVE_SSPI", L"1");
}

SessionSecret::SessionSecret(std::span<const char> bytes) : bytes_(bytes.begin(), bytes.end()) {
    bytes_.push_back('\0');
}
SessionSecret::~SessionSecret() {
    if (!bytes_.empty()) {
        SecureZeroMemory(bytes_.data(), bytes_.size());
    }
}
std::span<const char> SessionSecret::Bytes() const noexcept {
    return std::span<const char>{bytes_}.first(bytes_.size() - 1);
}
bool SessionSecret::IsValid() const noexcept {
    const auto bytes = Bytes();
    return bytes.size() >= 32 && bytes.size() <= 256 && std::ranges::none_of(bytes, [](char byte) { return byte == '\0'; });
}
bool SessionConfiguration::IsValid() const noexcept {
    return loopbackPort != 0 && IsWorkspaceAccount(account) && SafeName(domain) && domain.size() <= 15 && password && password->IsValid() &&
           ValidSize(desktop) &&
           proxyCertificateSha256.size() == 64 && std::ranges::all_of(proxyCertificateSha256, [](unsigned char byte) {
               return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F');
           });
}

struct RdpSession::State final : std::enable_shared_from_this<State> {
    // FreeRDP allocates the ABI prefix; only the weak reference tail is explicitly
    // constructed/destructed at ClientNew/ClientFree. No borrowed pointer is retained by project state.
    struct Context final {
        rdpClientContext base{};
        std::weak_ptr<State> owner{};
    };
    SessionConfiguration configuration{};
    SessionCallbacks callbacks{};
    std::atomic<std::shared_ptr<rdpContext>> context{};
    DisplayChannel display{};
    std::unique_ptr<ClipboardChannel> clipboard{};
    std::atomic<std::shared_ptr<const ClipboardContent>> pending_clipboard{};
    ClipboardContent local_clipboard{};
    UniqueWinHandle command_event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    std::atomic_bool stopping{false};
    std::atomic_bool audio_enabled{true};
    std::atomic_bool audio_sync_requested{true};
    std::mutex commands_mutex{};
    std::deque<Command> commands{};
    Rectangle dirty{};
    std::uint64_t frame_id{0};
    std::uint64_t in_flight{0};
    bool connected{false};
    std::chrono::steady_clock::time_point diagnostic_deadline{};

    State(SessionConfiguration config, SessionCallbacks cb)
        : configuration(std::move(config)), callbacks(std::move(cb)), audio_enabled(configuration.audio) {}
    static Context& Extended(rdpContext& borrowed) {
        return reinterpret_cast<Context&>(borrowed);
    }
    static std::shared_ptr<State> Owner(rdpContext& borrowed) {
        return Extended(borrowed).owner.lock();
    }

    void Report(SessionPhase phase, std::string reason) noexcept {
        try {
            callbacks.phase(phase, std::move(reason));
        } catch (...) {
            Stop();
        }
    }
    void Stop() {
        stopping.store(true);
        if (const auto current = context.load()) {
            static_cast<void>(freerdp_abort_connect_context(current.get()));
        }
        SetEvent(command_event.get());
    }
    void Enqueue(Command command) {
        if (stopping.load()) {
            return;
        }
        {
            std::lock_guard lock(commands_mutex);
            if (commands.size() >= 1024) {
                stopping.store(true);
            } else {
                commands.push_back(command);
            }
        }
        SetEvent(command_event.get());
    }

    void SetAudioEnabled(const bool enabled) {
        audio_enabled.store(configuration.audio && enabled);
        audio_sync_requested.store(true);
        SetEvent(command_event.get());
    }

    static BOOL GlobalInit() {
        return freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0) == CHANNEL_RC_OK;
    }
    static void GlobalUninit() {}
    static int ClientStart(rdpContext*) {
        return 0;
    } // NOLINT(pixels-raw-pointer-boundary): FreeRDP ABI.
    static int ClientStop(rdpContext*) {
        return 0;
    } // NOLINT(pixels-raw-pointer-boundary): FreeRDP ABI.
    static BOOL ClientNew(freerdp* instance, rdpContext* borrowed) { // NOLINT(pixels-raw-pointer-boundary): FreeRDP ABI.
        std::construct_at(&Extended(*borrowed).owner);
        instance->PreConnect = PreConnect;
        instance->PostConnect = PostConnect;
        instance->VerifyX509Certificate = VerifyCertificate;
        instance->AuthenticateEx = RefuseAuthenticationPrompt;
        instance->Redirect = RefuseRedirect;
        return TRUE;
    }
    static void ClientFree(freerdp*, rdpContext* borrowed) { // NOLINT(pixels-raw-pointer-boundary): FreeRDP ABI.
        std::destroy_at(&Extended(*borrowed).owner);
    }
    static BOOL RefuseAuthenticationPrompt( // FreeRDP authentication ABI; all arguments are borrowed only during this call.
        freerdp* instance, char** username, char** password, char** domain, rdp_auth_reason reason) { // NOLINT(pixels-raw-pointer-boundary)
        // NOLINT(pixels-raw-pointer-boundary): FreeRDP authentication ABI; validate preinstalled values without replacing any allocation.
        const auto self = Owner(*instance->context);
        if (!self || self->stopping.load() || reason != AUTH_NLA || !username || !*username || !password || !*password || !domain || !*domain) {
            return FALSE;
        }
        const auto expected = self->configuration.password->Bytes();
        const auto supplied = std::string_view{*password};
        return IsWorkspacePeer(self->configuration.account, self->configuration.domain, *username, *domain) && supplied.size() == expected.size() &&
               std::ranges::equal(supplied, expected);
    }
    static BOOL RefuseRedirect(freerdp*) {
        return FALSE;
    } // NOLINT(pixels-raw-pointer-boundary): FreeRDP ABI.
    static int VerifyCertificate( // FreeRDP certificate ABI; no argument is retained.
        freerdp* instance, const BYTE* bytes, size_t length, const char* host, UINT16 port, DWORD flags) { // NOLINT(pixels-raw-pointer-boundary)
        // NOLINT(pixels-raw-pointer-boundary): synchronous certificate callback ABI; no borrowed argument is retained.
        if (!host || !bytes) {
            return 0;
        }
        auto& current = *instance;
        const auto pem = std::span<const unsigned char>{bytes, length};
        const auto hostname = std::string_view{host};
        return Guard([&current, pem, hostname, port, flags] {
            const auto self = Owner(*current.context);
            return self && !self->stopping.load() && hostname == "127.0.0.1" && port == self->configuration.loopbackPort && flags == 0 &&
                   VerifyPinnedCertificate(pem, self->configuration.proxyCertificateSha256);
        });
    }
    static BOOL PreConnect(freerdp* instance) { // NOLINT(pixels-raw-pointer-boundary): FreeRDP ABI.
        auto& current = *instance;
        return Guard([&current] {
            const auto self = Owner(*current.context);
            if (!self || self->stopping.load()) {
                return false;
            }
            auto& settings = *current.context->settings;
            const auto& config = self->configuration;
            bool ok = freerdp_settings_set_string(&settings, FreeRDP_ServerHostname, "127.0.0.1") &&
                      freerdp_settings_set_uint32(&settings, FreeRDP_ServerPort, config.loopbackPort) &&
                      freerdp_settings_set_string(&settings, FreeRDP_Username, config.account.c_str()) &&
                      freerdp_settings_set_string(&settings, FreeRDP_Domain, config.domain.c_str()) &&
                      freerdp_settings_set_string(&settings, FreeRDP_Password, config.password->Bytes().data());
            // Channel callbacks and command processing share this protocol worker.
            // Static channel threads and the independent drdynvc worker both
            // need disabling; ThreadingFlags alone does not control drdynvc.
            for (const auto& setting : std::array<std::pair<FreeRDP_Settings_Keys_UInt32, UINT32>, 8>{
                     {{FreeRDP_DesktopWidth, static_cast<UINT32>(config.desktop.width)},
                      {FreeRDP_DesktopHeight, static_cast<UINT32>(config.desktop.height)},
                      {FreeRDP_ColorDepth, 32},
                      {FreeRDP_TcpConnectTimeout, 5000},
                      {FreeRDP_KeyboardLayout, 0x0409},
                      {FreeRDP_ThreadingFlags, THREADING_FLAGS_DISABLE_THREADS},
                      {FreeRDP_OsMajorType, OSMAJORTYPE_WINDOWS},
                      {FreeRDP_OsMinorType, OSMINORTYPE_WINDOWS_NT}}}) {
                ok = freerdp_settings_set_uint32(&settings, setting.first, setting.second) && ok;
            }
            for (const auto& setting :
                 std::array<std::pair<FreeRDP_Settings_Keys_Bool, BOOL>, 24>{{{FreeRDP_NlaSecurity, TRUE},
                                                                              {FreeRDP_TlsSecurity, TRUE},
                                                                              {FreeRDP_RdpSecurity, FALSE},
                                                                              {FreeRDP_ExternalCertificateManagement, TRUE},
                                                                              {FreeRDP_IgnoreCertificate, FALSE},
                                                                              {FreeRDP_AutoReconnectionEnabled, FALSE},
                                                                              {FreeRDP_GatewayEnabled, FALSE},
                                                                              {FreeRDP_SynchronousDynamicChannels, TRUE},
                                                                              {FreeRDP_SupportGraphicsPipeline, TRUE},
                                                                              {FreeRDP_GfxH264, TRUE},
                                                                              {FreeRDP_GfxAVC444, TRUE},
                                                                              {FreeRDP_GfxAVC444v2, TRUE},
                                                                              {FreeRDP_RemoteFxCodec, FALSE},
                                                                              {FreeRDP_NSCodec, FALSE},
                                                                              {FreeRDP_DesktopResize, TRUE},
                                                                              {FreeRDP_SupportDisplayControl, TRUE},
                                                                              {FreeRDP_SupportMonitorLayoutPdu, TRUE},
                                                                              {FreeRDP_AudioPlayback, config.audio ? TRUE : FALSE},
                                                                              {FreeRDP_AudioCapture, FALSE},
                                                                              {FreeRDP_RedirectClipboard, config.clipboard ? TRUE : FALSE},
                                                                              {FreeRDP_RedirectDrives, FALSE},
                                                                              {FreeRDP_RedirectPrinters, FALSE},
                                                                              {FreeRDP_RedirectSmartCards, FALSE},
                                                                              {FreeRDP_MultiTouchInput, FALSE}}}) {
                ok = freerdp_settings_set_bool(&settings, setting.first, setting.second) && ok;
            }
            PubSub_SubscribeChannelConnected(current.context->pubSub, ChannelConnected);
            PubSub_SubscribeChannelDisconnected(current.context->pubSub, ChannelDisconnected);
            return ok;
        });
    }
    static BOOL PostConnect(freerdp* instance) { // NOLINT(pixels-raw-pointer-boundary): FreeRDP ABI.
        auto& current = *instance;
        return Guard([&current] {
            const auto self = Owner(*current.context);
            if (!self || self->stopping.load() || !gdi_init(&current, PIXEL_FORMAT_BGRX32)) {
                return false;
            }
            auto& update = *current.context->update;
            update.BeginPaint = BeginPaint;
            update.EndPaint = EndPaint;
            update.DesktopResize = DesktopResize;
            self->connected = true;
            // A newly allocated GDI surface is not a remote frame. Only an
            // actual EndPaint damage notification may make the UI ready.
            self->dirty = {};
            self->Report(SessionPhase::Connected, {});
            return true;
        });
    }
    static void ChannelConnected(void* borrowed, const ChannelConnectedEventArgs* event) { // NOLINT(pixels-raw-pointer-boundary): ABI.
        if (!event || !event->name) {
            return;
        }
        auto& base = *static_cast<rdpContext*>(borrowed);
        const auto& update = *event;
        const auto result = Guard([&base, &update] {
            const auto self = Owner(base);
            if (!self) {
                return false;
            }
            if (std::string_view{update.name} == DISP_DVC_CHANNEL_NAME) {
                self->display.Attach(std::shared_ptr<DispClientContext>{self->context.load(), static_cast<DispClientContext*>(update.pInterface)});
            } else if (std::string_view{update.name} == CLIPRDR_SVC_CHANNEL_NAME) {
                if (!self->configuration.clipboard || !update.pInterface) return false;
                const auto channel =
                    std::shared_ptr<CliprdrClientContext>{self->context.load(), static_cast<CliprdrClientContext*>(update.pInterface)};
                channel->MonitorReady = ClipboardReady;
                channel->ServerCapabilities = ClipboardCapabilities;
                channel->ServerFormatList = ClipboardFormats;
                channel->ServerFormatListResponse = ClipboardFormatsResponse;
                channel->ServerFormatDataRequest = ClipboardDataRequest;
                channel->ServerFormatDataResponse = ClipboardDataResponse;
                channel->ServerFileContentsRequest = ClipboardFileRequest;
                channel->ServerFileContentsResponse = ClipboardFileResponse;
                const std::weak_ptr<State> weak{self};
                self->clipboard = std::make_unique<ClipboardChannel>(channel, [weak](ClipboardContent content) {
                    if (const auto owner = weak.lock(); owner && !owner->stopping.load() && owner->callbacks.clipboard) {
                        owner->callbacks.clipboard(std::move(content));
                    }
                });
                if (!self->local_clipboard.Empty() && !self->clipboard->SetLocal(self->local_clipboard)) return false;
            } else {
                freerdp_client_OnChannelConnectedEventHandler(&base, &update);
            }
            return true;
        });
        if (!result) {
            if (const auto self = Owner(*static_cast<rdpContext*>(borrowed))) {
                self->Stop();
            }
        }
    }
    static void ChannelDisconnected(void* borrowed, const ChannelDisconnectedEventArgs* event) { // NOLINT(pixels-raw-pointer-boundary): ABI.
        if (const auto self = Owner(*static_cast<rdpContext*>(borrowed)); self && event && event->name) {
            if (std::string_view{event->name} == DISP_DVC_CHANNEL_NAME) {
                self->display.Attach({});
            } else if (std::string_view{event->name} == CLIPRDR_SVC_CHANNEL_NAME) {
                self->clipboard.reset();
            } else {
                freerdp_client_OnChannelDisconnectedEventHandler(borrowed, event);
            }
        }
    }

    template <typename Action> static UINT WithClipboard(CliprdrClientContext& channel, Action action) noexcept {
        return Guard([&channel, &action] {
            if (!channel.rdpcontext) return false;
            const auto self = Owner(*channel.rdpcontext);
            return self && !self->stopping.load() && self->clipboard && action(*self->clipboard);
        })
                   ? CHANNEL_RC_OK
                   : ERROR_INVALID_DATA;
    }
    static UINT ClipboardReady(CliprdrClientContext* channel, const CLIPRDR_MONITOR_READY*) { // NOLINT(pixels-raw-pointer-boundary): ABI.
        return WithClipboard(*channel, [](ClipboardChannel& clipboard) { return clipboard.Ready(); });
    }
    static UINT ClipboardCapabilities(CliprdrClientContext* channel, const CLIPRDR_CAPABILITIES* value) { // NOLINT(pixels-raw-pointer-boundary)
        const auto& capabilities = *value;
        return WithClipboard(*channel, [&capabilities](ClipboardChannel& clipboard) { return clipboard.Capabilities(capabilities); });
    }
    static UINT ClipboardFormats(CliprdrClientContext* channel, const CLIPRDR_FORMAT_LIST* value) { // NOLINT(pixels-raw-pointer-boundary): ABI.
        const auto& formats = *value;
        return WithClipboard(*channel, [&formats](ClipboardChannel& clipboard) { return clipboard.Formats(formats); });
    }
    static UINT ClipboardFormatsResponse(CliprdrClientContext*, const CLIPRDR_FORMAT_LIST_RESPONSE*) { // NOLINT(pixels-raw-pointer-boundary): ABI.
        return CHANNEL_RC_OK;
    }
    static UINT ClipboardDataRequest( // NOLINT(pixels-raw-pointer-boundary): FreeRDP callback ABI.
        CliprdrClientContext* channel, const CLIPRDR_FORMAT_DATA_REQUEST* value) { // NOLINT(pixels-raw-pointer-boundary): callback ABI.
        const auto& request = *value;
        return WithClipboard(*channel, [&request](ClipboardChannel& clipboard) { return clipboard.DataRequest(request); });
    }
    static UINT ClipboardDataResponse( // NOLINT(pixels-raw-pointer-boundary): FreeRDP callback ABI.
        CliprdrClientContext* channel, const CLIPRDR_FORMAT_DATA_RESPONSE* value) { // NOLINT(pixels-raw-pointer-boundary): callback ABI.
        const auto& response = *value;
        return WithClipboard(*channel, [&response](ClipboardChannel& clipboard) { return clipboard.DataResponse(response); });
    }
    static UINT ClipboardFileRequest(  // NOLINT(pixels-raw-pointer-boundary): FreeRDP callback ABI.
        CliprdrClientContext* channel, const CLIPRDR_FILE_CONTENTS_REQUEST* value) {  // NOLINT(pixels-raw-pointer-boundary): callback ABI.
        const auto& request = *value;
        return WithClipboard(*channel, [&request](ClipboardChannel& clipboard) { return clipboard.FileRequest(request); });
    }
    static UINT ClipboardFileResponse(  // NOLINT(pixels-raw-pointer-boundary): FreeRDP callback ABI.
        CliprdrClientContext* channel, const CLIPRDR_FILE_CONTENTS_RESPONSE* value) {  // NOLINT(pixels-raw-pointer-boundary): callback ABI.
        const auto& response = *value;
        return WithClipboard(*channel, [&response](ClipboardChannel& clipboard) { return clipboard.FileResponse(response); });
    }
    static BOOL BeginPaint(rdpContext* borrowed) { // NOLINT(pixels-raw-pointer-boundary): FreeRDP ABI.
        if (!borrowed->gdi || !borrowed->gdi->primary || !borrowed->gdi->primary->hdc || !borrowed->gdi->primary->hdc->hwnd) {
            return FALSE;
        }
        auto& window = *borrowed->gdi->primary->hdc->hwnd;
        if (!window.invalid) {
            return FALSE;
        }
        window.invalid->null = TRUE;
        window.ninvalid = 0;
        return TRUE;
    }
    static BOOL EndPaint(rdpContext* borrowed) { // NOLINT(pixels-raw-pointer-boundary): FreeRDP ABI.
        auto& current = *borrowed;
        return Guard([&current] {
            const auto self = Owner(current);
            if (!self || !current.gdi || !current.gdi->primary || !current.gdi->primary->hdc || !current.gdi->primary->hdc->hwnd) {
                return false;
            }
            const auto& window = *current.gdi->primary->hdc->hwnd;
            if (!window.invalid || window.invalid->null) {
                return true;
            }
            if (window.ninvalid < 0 || window.ninvalid > 4096) {
                return false;
            }
            for (int index{}; index < window.ninvalid; ++index) {
                const auto& region = window.cinvalid[index];
                const Rectangle rectangle{region.x, region.y, region.w, region.h};
                if (rectangle.x < 0 || rectangle.y < 0 || rectangle.width < 0 || rectangle.height < 0 || rectangle.width > current.gdi->width ||
                    rectangle.height > current.gdi->height || rectangle.x > current.gdi->width - rectangle.width ||
                    rectangle.y > current.gdi->height - rectangle.height) {
                    return false;
                }
                self->dirty = Unite(self->dirty, rectangle);
            }
            return self->Publish(current);
        });
    }
    bool Publish(rdpContext& borrowed) {
        if (in_flight != 0 || dirty.Empty() || stopping.load()) {
            return true;
        }
        if (!borrowed.gdi || !borrowed.gdi->primary_buffer) {
            return false;
        }
        const auto& gdi = *borrowed.gdi;
        if (!ValidSize({gdi.width, gdi.height}) || gdi.stride < static_cast<UINT32>(gdi.width) * 4) {
            return false;
        }
        auto frame = std::make_shared<DesktopFrame>();
        frame->frameId = ++frame_id;
        frame->capturedMicroseconds =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        frame->desktop = {gdi.width, gdi.height};
        const auto rectangle = Intersect(dirty, {0, 0, gdi.width, gdi.height});
        if (rectangle.Empty()) {
            return false;
        }
        frame->rectangles.push_back(rectangle);
        const auto row_bytes = static_cast<std::size_t>(rectangle.width) * 4U;
        frame->pixels.resize(static_cast<std::size_t>(rectangle.height) * row_bytes);
        for (int row{}; row < rectangle.height; ++row) {
            std::memcpy(frame->pixels.data() + row * row_bytes,
                        gdi.primary_buffer + static_cast<std::size_t>(rectangle.y + row) * gdi.stride + static_cast<std::size_t>(rectangle.x) * 4U,
                        row_bytes); // Synchronous borrow of the FreeRDP-owned surface.
        }
        dirty = {};
        in_flight = frame->frameId;
        callbacks.frame(std::move(frame));
        return true;
    }
    static BOOL DesktopResize(rdpContext* borrowed) { // NOLINT(pixels-raw-pointer-boundary): FreeRDP ABI.
        auto& current = *borrowed;
        return Guard([&current] {
            const auto self = Owner(current);
            const auto width = freerdp_settings_get_uint32(current.settings, FreeRDP_DesktopWidth);
            const auto height = freerdp_settings_get_uint32(current.settings, FreeRDP_DesktopHeight);
            LOGI("event=rdp.desktop.resize width={} height={}", width, height);
            if (!self || width > 8192 || height > 8192 || !ValidSize({static_cast<int>(width), static_cast<int>(height)}) || !current.gdi ||
                !gdi_resize(current.gdi, width, height)) {
                return false;
            }
            // Resizing allocates a blank surface; discard old-size damage and
            // await the server's paint instead of presenting allocation as data.
            self->dirty = {};
            return true;
        });
    }
    bool ProcessCommands(rdpContext& borrowed) {
        const auto diagnostic_now = std::chrono::steady_clock::now();
        if (diagnostic_now >= diagnostic_deadline) {
            diagnostic_deadline = diagnostic_now + std::chrono::seconds(10);
            LOGI("event=rdp.frame.progress published={} in_flight={} dirty={} display_channel={}", frame_id, in_flight, !dirty.Empty(),
                 display.Connected());
        }
        if (const auto changed = pending_clipboard.exchange({})) {
            local_clipboard = *changed;
            if (clipboard && !clipboard->SetLocal(local_clipboard)) return false;
        }
        if (clipboard && !clipboard->Tick()) return false;
        std::deque<Command> current{};
        {
            std::lock_guard lock(commands_mutex);
            current.swap(commands);
        }
        for (const auto& command : current) {
            if (stopping.load()) {
                break;
            }
            bool ok{true};
            switch (command.kind) {
            case CommandKind::kMouse:
                if (command.x < 0 || command.y < 0 || command.x > 8191 || command.y > 8191) {
                    break;
                }
                ok = command.flag ? freerdp_input_send_extended_mouse_event(borrowed.input, static_cast<UINT16>(command.value), command.x, command.y)
                                  : freerdp_input_send_mouse_event(borrowed.input, static_cast<UINT16>(command.value), command.x, command.y);
                break;
            case CommandKind::kKey:
                ok = freerdp_input_send_keyboard_event_ex(borrowed.input, command.flag, FALSE, static_cast<UINT32>(command.value));
                break;
            case CommandKind::kUnicode:
                ok = freerdp_input_send_unicode_keyboard_event(borrowed.input, command.flag ? 0 : KBD_FLAGS_RELEASE,
                                                               static_cast<UINT16>(command.value));
                break;
            case CommandKind::kSynchronize:
                ok = freerdp_input_send_synchronize_event(borrowed.input, static_cast<UINT32>(command.value));
                break;
            case CommandKind::kPause:
                ok = freerdp_input_send_keyboard_pause_event(borrowed.input);
                break;
            case CommandKind::kResize: {
                if (!ValidSize({command.x, command.y})) {
                    break;
                }
                display.Request({command.x, command.y});
                break;
            }
            case CommandKind::kFrameConsumed:
                if (command.value == in_flight) {
                    in_flight = 0;
                    ok = Publish(borrowed);
                }
                break;
            case CommandKind::kRefresh: {
                if (!borrowed.gdi) {
                    break;
                }
                const RECTANGLE_16 area{0, 0, static_cast<UINT16>(borrowed.gdi->width - 1), static_cast<UINT16>(borrowed.gdi->height - 1)};
                ok = borrowed.update->RefreshRect(&borrowed, 1, &area);
                break;
            }
            }
            if (!ok) {
                return false;
            }
        }
        return display.Flush();
    }

    void Run() noexcept {
        bool success{false};
        try {
            ProcessAudioController audioController{};
            auto nextAudioSynchronization = std::chrono::steady_clock::now();
            std::optional<bool> appliedAudioMuted{};
            Report(SessionPhase::Connecting, {});
            RDP_CLIENT_ENTRY_POINTS entry{};
            entry.Size = sizeof(entry);
            entry.Version = RDP_CLIENT_INTERFACE_VERSION;
            entry.ContextSize = sizeof(Context);
            entry.GlobalInit = GlobalInit;
            entry.GlobalUninit = GlobalUninit;
            entry.ClientNew = ClientNew;
            entry.ClientFree = ClientFree;
            entry.ClientStart = ClientStart;
            entry.ClientStop = ClientStop;
            const auto current = std::shared_ptr<rdpContext>{freerdp_client_context_new(&entry), ContextCloser{}};
            if (!current) {
                throw std::runtime_error("RDP context initialization failed");
            }
            Extended(*current).owner = weak_from_this();
            context.store(current);
            if (!stopping.load() && freerdp_connect(current->instance)) {
                success = true;
                while (!stopping.load() && !freerdp_shall_disconnect_context(current.get())) {
                    if (!ProcessCommands(*current)) {
                        success = false;
                        break;
                    }
                    const auto now = std::chrono::steady_clock::now();
                    if (configuration.audio && (audio_sync_requested.exchange(false) || now >= nextAudioSynchronization)) {
                        const bool muted = !audio_enabled.load();
                        if (audioController.ApplyMuted(muted) && appliedAudioMuted != muted) {
                            appliedAudioMuted = muted;
                            LOGI("event=rdp.audio.mute applied={}", muted);
                        }
                        nextAudioSynchronization = now + (muted ? std::chrono::milliseconds{250} : std::chrono::seconds{2});
                    }
                    // Transient borrowed WinPR/Win32 wait ABI; no event ownership is transferred or retained.
                    std::array<HANDLE, MAXIMUM_WAIT_OBJECTS> handles{};
                    DWORD count = freerdp_get_event_handles(current.get(), handles.data(), static_cast<DWORD>(handles.size() - 1));
                    if (count == 0 || count >= handles.size()) {
                        success = false;
                        break;
                    }
                    handles[count++] = command_event.get();
                    const auto wait = WaitForMultipleObjects(count, handles.data(), FALSE, 100);
                    if (wait == WAIT_FAILED || !freerdp_check_event_handles(current.get())) {
                        success = false;
                        break;
                    }
                }
            }
            if (!success && !stopping.load()) {
                Report(SessionPhase::Failed, "RDP connection failed, code=" + std::to_string(freerdp_get_last_error(current.get())));
            }
            static_cast<void>(freerdp_disconnect(current->instance));
            display.Attach({});
            clipboard.reset();
            context.store({});
        } catch (...) {
            if (const auto current = context.exchange({})) {
                static_cast<void>(freerdp_disconnect(current->instance));
            }
            display.Attach({});
            clipboard.reset();
            Report(SessionPhase::Failed, "RDP protocol initialization or processing failed");
        }
        connected = false;
        stopping.store(true);
        configuration.password.reset();
        {
            std::lock_guard lock(commands_mutex);
            commands.clear();
        }
        Report(SessionPhase::Disconnected, {});
    }
};

std::shared_ptr<RdpSession> RdpSession::Create(SessionConfiguration configuration, SessionCallbacks callbacks) {
    if (!configuration.IsValid() || !callbacks.frame || !callbacks.phase) {
        return {};
    }
    auto state = std::make_shared<State>(std::move(configuration), std::move(callbacks));
    if (!state->command_event) {
        return {};
    }
    auto self = std::make_shared<RdpSession>(ConstructionKey{}, state);
    self->thread_ = std::jthread{[state](std::stop_token) { state->Run(); }};
    return self;
}
RdpSession::RdpSession(ConstructionKey, std::shared_ptr<State> state) : state_(std::move(state)) {}
RdpSession::~RdpSession() {
    Stop();
    // The existing runtime reaper owns the join, including destruction from a callback.
    if (thread_.joinable()) {
        PxAsyncRuntime::DeferJoin(std::move(thread_));
    }
}
void RdpSession::Stop() {
    state_->Stop();
}
void RdpSession::Mouse(std::uint16_t flags, int x, int y, bool extended) {
    state_->Enqueue({CommandKind::kMouse, flags, x, y, extended});
}
void RdpSession::Key(std::uint32_t scancode, bool down) {
    state_->Enqueue({CommandKind::kKey, scancode, 0, 0, down});
}
void RdpSession::Unicode(std::uint16_t codepoint, bool down) {
    state_->Enqueue({CommandKind::kUnicode, codepoint, 0, 0, down});
}
void RdpSession::Synchronize(std::uint16_t toggles) {
    state_->Enqueue({CommandKind::kSynchronize, toggles});
}
void RdpSession::Pause() {
    state_->Enqueue({CommandKind::kPause});
}
void RdpSession::Resize(const Size size) {
    state_->Enqueue({CommandKind::kResize, 0, size.width, size.height});
}
void RdpSession::ConsumeFrame(std::uint64_t frame_id) {
    state_->Enqueue({CommandKind::kFrameConsumed, frame_id});
}
void RdpSession::Refresh() {
    state_->Enqueue({CommandKind::kRefresh});
}
void RdpSession::PublishClipboard(ClipboardContent content) {
    if (!state_->configuration.clipboard || state_->stopping.load()) return;
    state_->pending_clipboard.store(std::make_shared<const ClipboardContent>(std::move(content)));
    SetEvent(state_->command_event.get());
}

void RdpSession::SetAudioEnabled(const bool enabled) {
    state_->SetAudioEnabled(enabled);
}

} // namespace px::rdp
