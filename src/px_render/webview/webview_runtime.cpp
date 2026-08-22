#include "webview_runtime.h"

#include <Windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_audio_handler.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_dialog_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_parser.h"
#include "include/cef_permission_handler.h"
#include "include/cef_render_handler.h"
#include "include/cef_request_handler.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include "px_common_new/data.h"
#include "px_common_new/image.h"
#include "px_common_new/log.h"

namespace px {
namespace {

using Microsoft::WRL::ComPtr;

constexpr char kWebViewDisplayName[] = "webview";

class UiClosureTask final : public CefTask {
public:
    explicit UiClosureTask(std::function<void()> closure)
        : closure_(std::move(closure)) {}

    void Execute() override { closure_(); }

private:
    std::function<void()> closure_;
    IMPLEMENT_REFCOUNTING(UiClosureTask);
};

void PostToCefUi(std::function<void()> closure) {
    CefPostTask(TID_UI, new UiClosureTask(std::move(closure)));
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool IsInternalHttpHost(const std::string& raw_host) {
    const auto host = LowerAscii(raw_host);
    if (host == "localhost" ||
        (host.size() > 6 && host.ends_with(".local"))) {
        return true;
    }
    unsigned int a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (sscanf_s(host.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail, 1) == 4 &&
        a <= 255 && b <= 255 && c <= 255 && d <= 255) {
        return a == 10 || a == 127 ||
            (a == 172 && b >= 16 && b <= 31) ||
            (a == 192 && b == 168) ||
            (a == 169 && b == 254);
    }
    return host == "::1" || host.starts_with("fc") || host.starts_with("fd");
}

bool ParseAllowedUrl(const std::string& url, std::string* origin) {
    CefURLParts parts{};
    if (!CefParseURL(url, parts)) return false;
    const auto scheme = LowerAscii(CefString(&parts.scheme).ToString());
    const auto host = LowerAscii(CefString(&parts.host).ToString());
    const auto username = CefString(&parts.username).ToString();
    const auto password = CefString(&parts.password).ToString();
    if ((scheme != "http" && scheme != "https") || host.empty() ||
        !username.empty() || !password.empty()) {
        return false;
    }
    if (scheme == "http" && !IsInternalHttpHost(host)) {
        return false;
    }
    if (origin) {
        auto port = CefString(&parts.port).ToString();
        if (port.empty()) port = scheme == "https" ? "443" : "80";
        *origin = scheme + "://" + host + ":" + port;
    }
    return true;
}

std::string DecodeAndValidateUrl(const std::string& encoded,
                                 std::string& origin,
                                 std::string& error) {
    if (encoded.empty()) {
        error = "webview URL is empty";
        return {};
    }
    std::string standard = encoded;
    std::replace(standard.begin(), standard.end(), '-', '+');
    std::replace(standard.begin(), standard.end(), '_', '/');
    while ((standard.size() % 4) != 0) {
        standard.push_back('=');
    }
    auto binary = CefBase64Decode(standard);
    if (!binary || binary->GetSize() == 0 || binary->GetSize() > 8192) {
        error = "webview URL encoding is invalid";
        return {};
    }
    std::string url(binary->GetSize(), '\0');
    if (binary->GetData(url.data(), url.size(), 0) != url.size()
        || url.find('\0') != std::string::npos) {
        error = "webview URL encoding is invalid";
        return {};
    }

    if (!ParseAllowedUrl(url, &origin)) {
        error = "webview URL scheme, host, credentials or HTTP target are not allowed";
        return {};
    }
    return url;
}

bool IsAllowedNavigation(const std::string& url, const std::string& entry_origin) {
    std::string target_origin;
    return ParseAllowedUrl(url, &target_origin) && target_origin == entry_origin;
}

uint32_t MapCursorType(cef_cursor_type_t type) {
    switch (type) {
    case CT_IBEAM:
    case CT_VERTICALTEXT:
        return CursorInfoSync::kIdcIBeam;
    case CT_WAIT:
    case CT_PROGRESS:
        return CursorInfoSync::kIdcWait;
    case CT_CROSS:
    case CT_CELL:
        return CursorInfoSync::kIdcCross;
    case CT_NORTHRESIZE:
    case CT_SOUTHRESIZE:
    case CT_NORTHSOUTHRESIZE:
    case CT_ROWRESIZE:
        return CursorInfoSync::kIdcSizeNS;
    case CT_EASTRESIZE:
    case CT_WESTRESIZE:
    case CT_EASTWESTRESIZE:
    case CT_COLUMNRESIZE:
        return CursorInfoSync::kIdcSizeWE;
    case CT_NORTHWESTRESIZE:
    case CT_SOUTHEASTRESIZE:
    case CT_NORTHWESTSOUTHEASTRESIZE:
        return CursorInfoSync::kIdcSizeNWSE;
    case CT_NORTHEASTRESIZE:
    case CT_SOUTHWESTRESIZE:
    case CT_NORTHEASTSOUTHWESTRESIZE:
        return CursorInfoSync::kIdcSizeNESW;
    case CT_MOVE:
    case CT_MIDDLEPANNING:
        return CursorInfoSync::kIdcSizeAll;
    case CT_HAND:
    case CT_GRAB:
    case CT_GRABBING:
        return CursorInfoSync::kIdcHand;
    case CT_HELP:
        return CursorInfoSync::kIdcHelp;
    default:
        return CursorInfoSync::kIdcArrow;
    }
}

std::wstring MakeProfilePath(const std::string& instance_id) {
    std::wstring safe;
    safe.reserve(instance_id.size());
    for (const unsigned char ch : instance_id) {
        if (std::isalnum(ch) || ch == '-' || ch == '_') {
            safe.push_back(static_cast<wchar_t>(ch));
        }
    }
    if (safe.empty()) {
        safe = L"standalone";
    }
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    return (std::filesystem::path(temp) / "GammaRayPremium" / "cef" / safe).wstring();
}

class WebViewCefApp final : public CefApp, public CefBrowserProcessHandler {
public:
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }

    void OnBeforeCommandLineProcessing(const CefString& process_type,
                                       CefRefPtr<CefCommandLine> command_line) override {
        command_line->AppendSwitch("disable-extensions");
        command_line->AppendSwitch("disable-background-timer-throttling");
        command_line->AppendSwitch("disable-renderer-backgrounding");
        command_line->AppendSwitch("disable-backgrounding-occluded-windows");
        command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
        command_line->AppendSwitchWithValue("use-angle", "d3d11");
    }

    void OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line) override {
        // Chromium normally inherits every browser-process switch. CEF
        // children need none of the application credentials or entry URL.
        constexpr const char* sensitive_switches[] = {
            "webview_url_b64", "service_ipc_token", "device_random_pwd",
            "device_safety_pwd", "appkey", "push_rtmp_url",
        };
        for (const auto* name : sensitive_switches) command_line->RemoveSwitch(name);
    }

private:
    IMPLEMENT_REFCOUNTING(WebViewCefApp);
};

class WebViewClient final : public CefClient,
                            public CefDisplayHandler,
                            public CefRenderHandler,
                            public CefAudioHandler,
                            public CefLifeSpanHandler,
                            public CefLoadHandler,
                            public CefRequestHandler,
                            public CefDownloadHandler,
                            public CefDialogHandler,
                            public CefPermissionHandler {
public:
    WebViewClient(WebViewRuntimeConfig config, WebViewRuntimeCallbacks callbacks)
        : config_(std::move(config)), callbacks_(std::move(callbacks)) {}

    ~WebViewClient() override { ResetAcceleratedPaint(); }

    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefAudioHandler> GetAudioHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
    CefRefPtr<CefDialogHandler> GetDialogHandler() override { return this; }
    CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }

    bool OnCursorChange(CefRefPtr<CefBrowser>, CefCursorHandle,
                        cef_cursor_type_t type,
                        const CefCursorInfo& custom_cursor_info) override {
        if (!callbacks_.on_cursor) {
            return true;
        }
        CaptureCursorBitmap cursor{};
        cursor.visible_ = type != CT_NONE;
        cursor.type_ = MapCursorType(type);
        if (type == CT_CUSTOM && custom_cursor_info.buffer &&
            custom_cursor_info.size.width > 0 && custom_cursor_info.size.height > 0) {
            cursor.width_ = static_cast<uint32_t>(custom_cursor_info.size.width);
            cursor.height_ = static_cast<uint32_t>(custom_cursor_info.size.height);
            cursor.hotspot_x_ = custom_cursor_info.hotspot.x;
            cursor.hotspot_y_ = custom_cursor_info.hotspot.y;
            const auto size = static_cast<int64_t>(cursor.width_) * cursor.height_ * 4;
            cursor.data_ = Data::Make(
                static_cast<const char*>(custom_cursor_info.buffer), size);
        }
        callbacks_.on_cursor(cursor);
        return true;
    }

    void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override {
        rect = CefRect(0, 0, config_.width, config_.height);
    }

    bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& screen_info) override {
        // Constrain native <select>/autocomplete popups to the captured view.
        // Without a valid screen rectangle Chromium may place an OSR popup
        // outside the texture even though the page itself is in bounds.
        const CefRect view(0, 0, config_.width, config_.height);
        screen_info.rect = view;
        screen_info.available_rect = view;
        screen_info.device_scale_factor = 1.0f;
        return true;
    }

    void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override {
        CEF_REQUIRE_UI_THREAD();
        popup_visible_ = show;
        if (!show) {
            popup_original_rect_ = CefRect();
            popup_rect_ = CefRect();
            software_popup_.clear();
            software_popup_width_ = software_popup_height_ = 0;
            popup_texture_.Reset();
            // Popup paint is a separate surface. Emit the cached main view now
            // so a closed menu does not remain burned into the outgoing frame.
            EmitSoftwareComposite();
            EmitAcceleratedComposite();
            if (browser) browser->GetHost()->Invalidate(PET_VIEW);
        }
    }

    void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& rect) override {
        CEF_REQUIRE_UI_THREAD();
        if (rect.width <= 0 || rect.height <= 0) return;
        popup_original_rect_ = rect;
        popup_rect_ = AdjustPopupRect(rect);
        LOGI("WebView popup surface: original=({},{} {}x{}) composite=({},{} {}x{})",
             rect.x, rect.y, rect.width, rect.height,
             popup_rect_.x, popup_rect_.y, popup_rect_.width, popup_rect_.height);
    }

    void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type,
                  const RectList&, const void* buffer, int width, int height) override {
        CEF_REQUIRE_UI_THREAD();
        if (!buffer || width <= 0 || height <= 0 || !active_) {
            return;
        }
        const auto bytes = static_cast<size_t>(width) * height * 4;
        if (type == PET_VIEW) {
            software_view_.assign(static_cast<const uint8_t*>(buffer),
                                  static_cast<const uint8_t*>(buffer) + bytes);
            software_view_width_ = width;
            software_view_height_ = height;
        } else if (type == PET_POPUP) {
            software_popup_.assign(static_cast<const uint8_t*>(buffer),
                                   static_cast<const uint8_t*>(buffer) + bytes);
            software_popup_width_ = width;
            software_popup_height_ = height;
        } else {
            return;
        }
        EmitSoftwareComposite();
        if (type == PET_VIEW) ObservePaintAndMaybeNotifyFirstFrame();
    }

    void OnAcceleratedPaint(CefRefPtr<CefBrowser>, PaintElementType type,
                            const RectList&, const CefAcceleratedPaintInfo& info) override {
        CEF_REQUIRE_UI_THREAD();
        if (!active_ || !info.shared_texture_handle) {
            return;
        }
        ComPtr<ID3D11Texture2D> source;
        if (!OpenAcceleratedTexture(info.shared_texture_handle, source)) {
            if (++accelerated_failures_ == 1) {
                LOGE("WebView accelerated paint could not open the CEF shared texture");
            }
            return;
        }
        D3D11_TEXTURE2D_DESC source_desc{};
        source->GetDesc(&source_desc);
        if (type == PET_VIEW && !EnsureBridgeTextures(source_desc)) {
            if (++accelerated_failures_ == 1) {
                LOGE("WebView accelerated paint could not create bridge textures");
            }
            return;
        }
        if (type == PET_VIEW) {
            if (!EnsureOwnedTexture(view_texture_, source_desc)) return;
            d3d_context_->CopyResource(view_texture_.Get(), source.Get());
        } else if (type == PET_POPUP) {
            if (!EnsureOwnedTexture(popup_texture_, source_desc)) return;
            d3d_context_->CopyResource(popup_texture_.Get(), source.Get());
        } else {
            return;
        }
        accelerated_failures_ = 0;
        EmitAcceleratedComposite();
        if (type == PET_VIEW) ObservePaintAndMaybeNotifyFirstFrame();
    }

    bool GetAudioParameters(CefRefPtr<CefBrowser>, CefAudioParameters& params) override {
        params.sample_rate = 48000;
        return config_.enable_audio;
    }

    void OnAudioStreamStarted(CefRefPtr<CefBrowser>,
                              const CefAudioParameters& params,
                              int channels) override {
        audio_sample_rate_ = params.sample_rate;
        audio_channels_ = channels;
        LOGI("WebView CEF audio started: {} Hz, {} channels", params.sample_rate, channels);
    }

    void OnAudioStreamPacket(CefRefPtr<CefBrowser>, const float** data,
                             int frames, int64_t) override {
        if (!active_ || !data || frames <= 0 || audio_channels_ <= 0
            || !callbacks_.on_audio_frame) {
            return;
        }
        const int channels = std::min(audio_channels_.load(), 8);
        std::vector<int16_t> pcm(static_cast<size_t>(frames) * channels);
        for (int frame = 0; frame < frames; ++frame) {
            for (int channel = 0; channel < channels; ++channel) {
                const auto sample = std::clamp(data[channel][frame], -1.0f, 1.0f);
                pcm[static_cast<size_t>(frame) * channels + channel] =
                    static_cast<int16_t>(std::lrint(sample * 32767.0f));
            }
        }
        CaptureAudioFrame audio{};
        audio.frame_index_ = ++audio_frame_index_;
        audio.samples_ = audio_sample_rate_;
        audio.channels_ = channels;
        audio.bits_ = 16;
        audio.full_data_ = Data::Make(reinterpret_cast<const char*>(pcm.data()),
                                      static_cast<int64_t>(pcm.size() * sizeof(int16_t)));
        callbacks_.on_audio_frame(audio);
    }

    void OnAudioStreamStopped(CefRefPtr<CefBrowser>) override {
        LOGI("WebView CEF audio stopped");
    }

    void OnAudioStreamError(CefRefPtr<CefBrowser>, const CefString& message) override {
        LOGE("WebView CEF audio error: {}", message.ToString());
    }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        CEF_REQUIRE_UI_THREAD();
        browser_ = browser;
        browser_->GetHost()->SetWindowlessFrameRate(active_ ? config_.frame_rate : 1);
        browser_->GetHost()->SetFocus(active_);
        LOGI("WebView CEF browser created");
    }

    bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, int,
                       const CefString& target_url, const CefString&,
                       CefLifeSpanHandler::WindowOpenDisposition,
                       bool, const CefPopupFeatures&, CefWindowInfo&,
                       CefRefPtr<CefClient>&, CefBrowserSettings&,
                       CefRefPtr<CefDictionaryValue>&, bool*) override {
        const auto url = target_url.ToString();
        if (!browser || !IsAllowedNavigation(url, config_.entry_origin)) {
            LOGW("WebView popup blocked by URL policy");
            return true;
        }

        // The render process owns one off-screen browser. Creating a second
        // CEF browser for target=_blank/window.open would have no surface or
        // input route. Redirect same-origin popups into the existing browser
        // after this callback has returned instead. Posting the navigation is
        // also important: loading synchronously from OnBeforePopup can re-enter
        // CEF's popup lifecycle (and has caused libcef access violations).
        PostToCefUi([browser, url] {
            if (auto frame = browser->GetMainFrame()) {
                frame->LoadURL(url);
            }
        });
        LOGI("WebView same-origin popup redirected into current view");
        return true;
    }

    void OnBeforeClose(CefRefPtr<CefBrowser>) override {
        CEF_REQUIRE_UI_THREAD();
        // CEF lifecycle callbacks can be entered synchronously from
        // SendKeyEvent/SendMouseClickEvent. Never inject another input event
        // while CEF is closing the browser; just discard our bookkeeping.
        ClearInputStateOnUi();
        browser_ = nullptr;
        {
            std::lock_guard lock(close_mutex_);
            closed_ = true;
        }
        close_cv_.notify_all();
        LOGI("WebView CEF browser closed");
    }

    void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                     ErrorCode error_code, const CefString&, const CefString&) override {
        // Subresource and iframe failures must not fail the whole scheduled
        // application. Only the entry/main document controls instance health.
        if (!frame || !frame->IsMain() || error_code == ERR_ABORTED) {
            return;
        }
        main_load_failed_ = true;
        LOGE("WebView page load failed: code={}", static_cast<int>(error_code));
        if (callbacks_.on_failed) {
            callbacks_.on_failed("WebView page load failed");
        }
    }

    void OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                     TransitionType) override {
        CEF_REQUIRE_UI_THREAD();
        if (frame && frame->IsMain()) {
            main_load_failed_ = false;
            main_load_succeeded_ = false;
            paint_seen_for_load_ = false;
            // A keyboard event (for example Enter in a form) may synchronously
            // start navigation before CefBrowserHost::SendKeyEvent returns.
            // Calling SendKeyEvent again from this callback re-enters libcef.
            // A new document cannot retain the old DOM input state, so clearing
            // local tracking is sufficient here.
            ClearInputStateOnUi();
        }
    }

    void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                   int http_status_code) override {
        if (frame && frame->IsMain()) {
            LOGI("WebView main frame load completed: http_status={}", http_status_code);
            if (http_status_code >= 400) {
                main_load_failed_ = true;
                if (callbacks_.on_failed) {
                    callbacks_.on_failed("WebView main page returned an HTTP error");
                }
            } else {
                main_load_succeeded_ = true;
                TryNotifyFirstFrame();
            }
        }
    }

    bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                        CefRefPtr<CefRequest> request, bool, bool) override {
        if (!request || IsAllowedNavigation(request->GetURL().ToString(), config_.entry_origin)) {
            return false;
        }
        LOGW("WebView navigation blocked by URL policy");
        return true;
    }

    bool OnOpenURLFromTab(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                          const CefString& target_url,
                          CefRequestHandler::WindowOpenDisposition, bool) override {
        return !IsAllowedNavigation(target_url.ToString(), config_.entry_origin);
    }

    bool OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem>,
                          const CefString&, CefRefPtr<CefBeforeDownloadCallback>) override {
        LOGW("WebView download blocked by policy");
        return true;
    }

    void OnDownloadUpdated(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem>,
                           CefRefPtr<CefDownloadItemCallback> callback) override {
        if (callback) {
            callback->Cancel();
        }
    }

    bool OnFileDialog(CefRefPtr<CefBrowser>, CefDialogHandler::FileDialogMode,
                      const CefString&, const CefString&,
                      const std::vector<CefString>&,
                      const std::vector<CefString>&,
                      const std::vector<CefString>&,
                      CefRefPtr<CefFileDialogCallback> callback) override {
        if (callback) callback->Cancel();
        LOGW("WebView file dialog blocked by policy");
        return true;
    }

    bool OnRequestMediaAccessPermission(
        CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString&, uint32_t,
        CefRefPtr<CefMediaAccessCallback> callback) override {
        if (callback) callback->Cancel();
        LOGW("WebView camera/microphone permission blocked by policy");
        return true;
    }

    bool OnShowPermissionPrompt(
        CefRefPtr<CefBrowser>, uint64_t, const CefString&, uint32_t,
        CefRefPtr<CefPermissionPromptCallback> callback) override {
        if (callback) callback->Continue(CEF_PERMISSION_RESULT_DENY);
        LOGW("WebView permission prompt denied by policy");
        return true;
    }

    void Close() {
        auto self = CefRefPtr<WebViewClient>(this);
        PostToCefUi([self] {
            auto target = self;
            if (target->browser_) {
                target->browser_->GetHost()->CloseBrowser(true);
            } else {
                std::lock_guard lock(target->close_mutex_);
                target->closed_ = true;
                target->close_cv_.notify_all();
            }
        });
    }

    bool WaitUntilClosed(std::chrono::milliseconds timeout) {
        std::unique_lock lock(close_mutex_);
        return close_cv_.wait_for(lock, timeout, [this] { return closed_; });
    }

    void SetActive(bool active) {
        auto self = CefRefPtr<WebViewClient>(this);
        PostToCefUi([self, active] {
            auto target = self;
            const bool value = active;
            if (!value) target->ReleaseInputOnUi();
            target->active_ = value;
            if (target->browser_) {
                target->browser_->GetHost()->SetWindowlessFrameRate(
                    value ? target->config_.frame_rate : 1);
                target->browser_->GetHost()->SetFocus(value);
                if (value) {
                    target->browser_->GetHost()->Invalidate(PET_VIEW);
                }
            }
        });
    }

    void SendMouse(const MouseEvent& event) {
        auto self = CefRefPtr<WebViewClient>(this);
        PostToCefUi([self, event] { self->SendMouseOnUi(event); });
    }

    void SendKey(const KeyEvent& event) {
        auto self = CefRefPtr<WebViewClient>(this);
        PostToCefUi([self, event] { self->SendKeyOnUi(event); });
    }

    void SendText(const TextInput& input) {
        auto self = CefRefPtr<WebViewClient>(this);
        PostToCefUi([self, input] { self->SendTextOnUi(input); });
    }

    void SendFocus(bool focused) {
        auto self = CefRefPtr<WebViewClient>(this);
        PostToCefUi([self, focused] {
            if (self->browser_) {
                if (!focused) self->ReleaseInputOnUi();
                self->browser_->GetHost()->SetFocus(focused);
            }
        });
    }

private:
    CefRect AdjustPopupRect(const CefRect& original) const {
        CefRect rect = original;
        rect.x = std::max(rect.x, 0);
        rect.y = std::max(rect.y, 0);
        if (rect.x + rect.width > config_.width) {
            rect.x = std::max(0, config_.width - rect.width);
        }
        if (rect.y + rect.height > config_.height) {
            rect.y = std::max(0, config_.height - rect.height);
        }
        return rect;
    }

    void ApplyPopupMouseOffset(int& x, int& y) const {
        if (!popup_visible_ || popup_rect_.width <= 0 || popup_rect_.height <= 0 ||
            x < popup_rect_.x || y < popup_rect_.y ||
            x >= popup_rect_.x + popup_rect_.width ||
            y >= popup_rect_.y + popup_rect_.height) {
            return;
        }
        x += popup_original_rect_.x - popup_rect_.x;
        y += popup_original_rect_.y - popup_rect_.y;
    }

    void EmitSoftwareComposite() {
        if (!active_ || software_view_.empty() || software_view_width_ <= 0 ||
            software_view_height_ <= 0 || !callbacks_.on_video_frame) {
            return;
        }
        auto composite = software_view_;
        if (popup_visible_ && !software_popup_.empty() &&
            software_popup_width_ > 0 && software_popup_height_ > 0) {
            const int dst_x = std::clamp(popup_rect_.x, 0, software_view_width_);
            const int dst_y = std::clamp(popup_rect_.y, 0, software_view_height_);
            const int copy_width = std::max(0, std::min(
                {software_popup_width_, popup_rect_.width, software_view_width_ - dst_x}));
            const int copy_height = std::max(0, std::min(
                {software_popup_height_, popup_rect_.height, software_view_height_ - dst_y}));
            for (int row = 0; row < copy_height; ++row) {
                const auto* src = software_popup_.data() +
                    static_cast<size_t>(row) * software_popup_width_ * 4;
                auto* dst = composite.data() +
                    (static_cast<size_t>(dst_y + row) * software_view_width_ + dst_x) * 4;
                std::memcpy(dst, src, static_cast<size_t>(copy_width) * 4);
            }
        }

        auto data = Data::Make(reinterpret_cast<const char*>(composite.data()),
                               static_cast<int64_t>(composite.size()));
        auto image = Image::Make(data, software_view_width_, software_view_height_, 4);
        image->raw_img_type_ = RawImageType::kBGRA;
        CaptureVideoFrame frame{};
        frame.capture_type_ = kCaptureVideoByBitmapData;
        frame.frame_width_ = software_view_width_;
        frame.frame_height_ = software_view_height_;
        frame.frame_index_ = ++frame_index_;
        frame.frame_format_ = DXGI_FORMAT_B8G8R8A8_UNORM;
        frame.adapter_uid_ = -1;
        frame.monitor_index_ = 0;
        frame.right_ = frame.frame_width_;
        frame.bottom_ = frame.frame_height_;
        frame.raw_image_ = std::move(image);
        strncpy_s(frame.display_name_, kWebViewDisplayName, _TRUNCATE);
        callbacks_.on_video_frame(frame);
    }

    bool EnsureOwnedTexture(ComPtr<ID3D11Texture2D>& texture,
                            const D3D11_TEXTURE2D_DESC& source_desc) {
        if (!d3d_device_) return false;
        if (texture) {
            D3D11_TEXTURE2D_DESC current{};
            texture->GetDesc(&current);
            if (current.Width == source_desc.Width && current.Height == source_desc.Height &&
                current.Format == source_desc.Format) {
                return true;
            }
            texture.Reset();
        }
        auto desc = source_desc;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        desc.BindFlags = 0;
        return SUCCEEDED(d3d_device_->CreateTexture2D(&desc, nullptr,
                                                       texture.ReleaseAndGetAddressOf()));
    }

    void EmitAcceleratedComposite() {
        if (!active_ || !d3d_context_ || !view_texture_ || !bridge_textures_[0] ||
            !callbacks_.on_video_frame) {
            return;
        }
        D3D11_TEXTURE2D_DESC view_desc{};
        view_texture_->GetDesc(&view_desc);
        const size_t index = bridge_write_index_;
        auto output = bridge_textures_[index].Get();
        d3d_context_->CopyResource(output, view_texture_.Get());

        if (popup_visible_ && popup_texture_ && popup_rect_.width > 0 &&
            popup_rect_.height > 0) {
            D3D11_TEXTURE2D_DESC popup_desc{};
            popup_texture_->GetDesc(&popup_desc);
            const int dst_x = std::clamp(popup_rect_.x, 0, static_cast<int>(view_desc.Width));
            const int dst_y = std::clamp(popup_rect_.y, 0, static_cast<int>(view_desc.Height));
            const int copy_width = std::max(0, std::min(
                {static_cast<int>(popup_desc.Width), popup_rect_.width,
                 static_cast<int>(view_desc.Width) - dst_x}));
            const int copy_height = std::max(0, std::min(
                {static_cast<int>(popup_desc.Height), popup_rect_.height,
                 static_cast<int>(view_desc.Height) - dst_y}));
            if (copy_width > 0 && copy_height > 0 && popup_desc.Format == view_desc.Format) {
                const D3D11_BOX source_box{
                    0, 0, 0, static_cast<UINT>(copy_width),
                    static_cast<UINT>(copy_height), 1};
                d3d_context_->CopySubresourceRegion(output, 0, dst_x, dst_y, 0,
                                                    popup_texture_.Get(), 0, &source_box);
            }
        }
        d3d_context_->Flush();
        bridge_write_index_ = (bridge_write_index_ + 1) % bridge_textures_.size();

        CaptureVideoFrame frame{};
        frame.capture_type_ = kCaptureVideoByHandle;
        frame.frame_width_ = static_cast<int>(view_desc.Width);
        frame.frame_height_ = static_cast<int>(view_desc.Height);
        frame.frame_index_ = ++frame_index_;
        frame.frame_format_ = view_desc.Format;
        frame.adapter_uid_ = adapter_uid_;
        frame.monitor_index_ = 0;
        frame.right_ = frame.frame_width_;
        frame.bottom_ = frame.frame_height_;
        frame.handle_ = reinterpret_cast<uint64_t>(bridge_handles_[index]);
        strncpy_s(frame.display_name_, kWebViewDisplayName, _TRUNCATE);
        callbacks_.on_video_frame(frame);
    }

    void ObservePaintAndMaybeNotifyFirstFrame() {
        paint_seen_for_load_ = true;
        TryNotifyFirstFrame();
    }

    void TryNotifyFirstFrame() {
        if (main_load_succeeded_ && paint_seen_for_load_ && !main_load_failed_ &&
            !first_frame_.exchange(true) && callbacks_.on_first_frame) {
            callbacks_.on_first_frame();
        }
    }

    bool OpenAcceleratedTexture(HANDLE handle, ComPtr<ID3D11Texture2D>& texture) {
        if (d3d_device_) {
            ComPtr<ID3D11Device1> device1;
            if (SUCCEEDED(d3d_device_.As(&device1)) &&
                SUCCEEDED(device1->OpenSharedResource1(handle, IID_PPV_ARGS(&texture))) && texture) {
                return true;
            }
            ResetAcceleratedPaint();
        }

        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            return false;
        }
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            ComPtr<ID3D11Device> device;
            ComPtr<ID3D11DeviceContext> context;
            D3D_FEATURE_LEVEL actual{};
            constexpr D3D_FEATURE_LEVEL levels[] = {
                D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
            };
            if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                         D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                         static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                         &device, &actual, &context))) {
                continue;
            }
            ComPtr<ID3D11Device1> device1;
            if (FAILED(device.As(&device1)) ||
                FAILED(device1->OpenSharedResource1(handle, IID_PPV_ARGS(&texture))) || !texture) {
                texture.Reset();
                continue;
            }
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            adapter_uid_ = static_cast<int64_t>(desc.AdapterLuid.LowPart);
            d3d_device_ = std::move(device);
            d3d_context_ = std::move(context);
            LOGI("WebView accelerated paint opened on adapter uid={}", adapter_uid_);
            return true;
        }
        return false;
    }

    bool EnsureBridgeTextures(const D3D11_TEXTURE2D_DESC& source_desc) {
        if (!d3d_device_ || source_desc.Width == 0 || source_desc.Height == 0) return false;
        if (bridge_textures_[0]) {
            D3D11_TEXTURE2D_DESC current{};
            bridge_textures_[0]->GetDesc(&current);
            if (current.Width == source_desc.Width && current.Height == source_desc.Height &&
                current.Format == source_desc.Format) {
                return true;
            }
            ResetBridgeTextures();
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = source_desc.Width;
        desc.Height = source_desc.Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = source_desc.Format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        for (size_t index = 0; index < bridge_textures_.size(); ++index) {
            if (FAILED(d3d_device_->CreateTexture2D(
                    &desc, nullptr, bridge_textures_[index].ReleaseAndGetAddressOf()))) {
                ResetBridgeTextures();
                return false;
            }
            ComPtr<IDXGIResource1> resource;
            if (FAILED(bridge_textures_[index].As(&resource)) ||
                FAILED(resource->CreateSharedHandle(nullptr,
                                                    DXGI_SHARED_RESOURCE_READ,
                                                    nullptr,
                                                    &bridge_handles_[index])) ||
                !bridge_handles_[index]) {
                ResetBridgeTextures();
                return false;
            }
        }
        bridge_write_index_ = 0;
        LOGI("WebView accelerated bridge created: {}x{} format={}",
             desc.Width, desc.Height, static_cast<int>(desc.Format));
        return true;
    }

    void ResetBridgeTextures() {
        for (auto& handle : bridge_handles_) {
            if (handle) CloseHandle(handle);
            handle = nullptr;
        }
        for (auto& texture : bridge_textures_) texture.Reset();
        bridge_write_index_ = 0;
    }

    void ResetAcceleratedPaint() {
        view_texture_.Reset();
        popup_texture_.Reset();
        ResetBridgeTextures();
        d3d_context_.Reset();
        d3d_device_.Reset();
        adapter_uid_ = -1;
    }

    int KeyboardModifiers() const {
        int modifiers = EVENTFLAG_NONE;
        if (control_down_) modifiers |= EVENTFLAG_CONTROL_DOWN;
        if (shift_down_) modifiers |= EVENTFLAG_SHIFT_DOWN;
        if (alt_down_) modifiers |= EVENTFLAG_ALT_DOWN;
        if (mouse_down_[0]) modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
        if (mouse_down_[1]) modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
        if (mouse_down_[2]) modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
        return modifiers;
    }

    int ClickCount(size_t button, int x, int y, int64_t timestamp) {
        const auto now = timestamp > 0 ? timestamp :
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        auto& state = clicks_[button];
        if (now - state.timestamp <= GetDoubleClickTime() &&
            std::abs(x - state.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
            std::abs(y - state.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2) {
            state.count = std::min(state.count + 1, 3);
        } else {
            state.count = 1;
        }
        state.timestamp = now;
        state.x = x;
        state.y = y;
        return state.count;
    }

    void SendMouseOnUi(const MouseEvent& value) {
        CEF_REQUIRE_UI_THREAD();
        if (!active_ || !browser_) return;
        CefMouseEvent event{};
        event.x = std::clamp(static_cast<int>(value.x_ratio() * config_.width), 0, config_.width - 1);
        event.y = std::clamp(static_cast<int>(value.y_ratio() * config_.height), 0, config_.height - 1);
        ApplyPopupMouseOffset(event.x, event.y);
        last_mouse_x_ = event.x;
        last_mouse_y_ = event.y;
        event.modifiers = KeyboardModifiers();
        auto host = browser_->GetHost();
        host->SendMouseMoveEvent(event, false);

        const int buttons = value.button();
        if ((buttons & ButtonFlag::kMouseEventWheel) != 0 || value.data() != 0) {
            host->SendMouseWheelEvent(event, value.delta_x(),
                                      value.delta_y() != 0 ? value.delta_y() : value.data());
        }
        const auto send_button = [&](size_t index, int down_flag, int up_flag,
                                     CefBrowserHost::MouseButtonType type) {
            const bool down = (buttons & down_flag) != 0 ||
                (value.pressed() && (buttons & (down_flag | up_flag)));
            const bool up = (buttons & up_flag) != 0 ||
                (value.released() && (buttons & (down_flag | up_flag)));
            if (down) {
                mouse_down_[index] = true;
                active_click_count_[index] = ClickCount(index, event.x, event.y, value.timestamp());
                event.modifiers = KeyboardModifiers();
                host->SendMouseClickEvent(event, type, false, active_click_count_[index]);
            }
            if (up) {
                mouse_down_[index] = false;
                event.modifiers = KeyboardModifiers();
                host->SendMouseClickEvent(event, type, true,
                                          std::max(active_click_count_[index], 1));
            }
        };
        send_button(0, ButtonFlag::kLeftMouseButtonDown, ButtonFlag::kLeftMouseButtonUp, MBT_LEFT);
        send_button(1, ButtonFlag::kMiddleMouseButtonDown, ButtonFlag::kMiddleMouseButtonUp, MBT_MIDDLE);
        send_button(2, ButtonFlag::kRightMouseButtonDown, ButtonFlag::kRightMouseButtonUp, MBT_RIGHT);
    }

    void SendKeyOnUi(const KeyEvent& value) {
        CEF_REQUIRE_UI_THREAD();
        if (!active_ || !browser_) return;
        const auto vk = value.key_code();
        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) control_down_ = value.down();
        if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) shift_down_ = value.down();
        if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) alt_down_ = value.down();
        if (value.down()) pressed_keys_.insert(vk);
        else pressed_keys_.erase(vk);

        CefKeyEvent event{};
        event.type = value.down() ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;
        event.windows_key_code = static_cast<int>(vk);
        event.native_key_code = static_cast<int>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16);
        event.modifiers = KeyboardModifiers();
        event.is_system_key = alt_down_;
        browser_->GetHost()->SendKeyEvent(event);

        // Text is delivered separately by kTextInput. Deriving characters
        // from VK here loses the client's keyboard layout and duplicates IME
        // commits; kKeyEvent remains the physical DOM key path.
    }

    void SendTextOnUi(const TextInput& value) {
        CEF_REQUIRE_UI_THREAD();
        if (!active_ || !browser_ || value.text().empty()) return;
        const auto chars = CefString(value.text()).ToString16();
        for (const char16_t ch : chars) {
            CefKeyEvent event{};
            event.type = KEYEVENT_CHAR;
            // Match the WM_CHAR shape used by CEF's own OSR client. Chromium
            // reads windows_key_code for character insertion; setting only
            // character/unmodified_character leaves text fields unchanged.
            event.windows_key_code = static_cast<int>(ch);
            event.native_key_code = 1; // WM_CHAR repeat count.
            event.character = ch;
            event.unmodified_character = ch;
            event.modifiers = KeyboardModifiers();
            browser_->GetHost()->SendKeyEvent(event);
        }
    }

    void ReleaseInputOnUi() {
        CEF_REQUIRE_UI_THREAD();
        if (!browser_) {
            pressed_keys_.clear();
            mouse_down_.fill(false);
            control_down_ = shift_down_ = alt_down_ = false;
            return;
        }
        auto host = browser_->GetHost();
        for (const auto vk : pressed_keys_) {
            CefKeyEvent event{};
            event.type = KEYEVENT_KEYUP;
            event.windows_key_code = static_cast<int>(vk);
            event.native_key_code = static_cast<int>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16);
            event.modifiers = KeyboardModifiers();
            host->SendKeyEvent(event);
        }
        pressed_keys_.clear();
        CefMouseEvent event{};
        event.x = last_mouse_x_;
        event.y = last_mouse_y_;
        constexpr CefBrowserHost::MouseButtonType types[] = {MBT_LEFT, MBT_MIDDLE, MBT_RIGHT};
        for (size_t index = 0; index < mouse_down_.size(); ++index) {
            if (mouse_down_[index]) {
                mouse_down_[index] = false;
                event.modifiers = KeyboardModifiers();
                host->SendMouseClickEvent(event, types[index], true,
                                          std::max(active_click_count_[index], 1));
            }
        }
        control_down_ = shift_down_ = alt_down_ = false;
    }

    void ClearInputStateOnUi() {
        CEF_REQUIRE_UI_THREAD();
        pressed_keys_.clear();
        mouse_down_.fill(false);
        active_click_count_.fill(0);
        control_down_ = shift_down_ = alt_down_ = false;
    }

    WebViewRuntimeConfig config_;
    WebViewRuntimeCallbacks callbacks_;
    CefRefPtr<CefBrowser> browser_;
    std::atomic_bool active_{false};
    std::atomic_bool first_frame_{false};
    std::atomic_bool main_load_failed_{false};
    std::atomic_bool main_load_succeeded_{false};
    std::atomic_bool paint_seen_for_load_{false};
    std::atomic_int audio_sample_rate_{48000};
    std::atomic_int audio_channels_{2};
    std::atomic_uint64_t frame_index_{0};
    std::atomic_uint64_t audio_frame_index_{0};
    ComPtr<ID3D11Device> d3d_device_;
    ComPtr<ID3D11DeviceContext> d3d_context_;
    std::array<ComPtr<ID3D11Texture2D>, 2> bridge_textures_{};
    std::array<HANDLE, 2> bridge_handles_{};
    ComPtr<ID3D11Texture2D> view_texture_;
    ComPtr<ID3D11Texture2D> popup_texture_;
    size_t bridge_write_index_ = 0;
    int64_t adapter_uid_ = -1;
    uint32_t accelerated_failures_ = 0;
    bool popup_visible_ = false;
    CefRect popup_original_rect_{};
    CefRect popup_rect_{};
    std::vector<uint8_t> software_view_;
    std::vector<uint8_t> software_popup_;
    int software_view_width_ = 0;
    int software_view_height_ = 0;
    int software_popup_width_ = 0;
    int software_popup_height_ = 0;
    struct ClickState { int64_t timestamp = 0; int x = 0; int y = 0; int count = 0; };
    std::array<ClickState, 3> clicks_{};
    std::array<int, 3> active_click_count_{};
    std::array<bool, 3> mouse_down_{};
    std::unordered_set<uint32_t> pressed_keys_;
    int last_mouse_x_ = 0;
    int last_mouse_y_ = 0;
    bool control_down_ = false;
    bool shift_down_ = false;
    bool alt_down_ = false;
    std::mutex close_mutex_;
    std::condition_variable close_cv_;
    bool closed_ = false;

    IMPLEMENT_REFCOUNTING(WebViewClient);
};

} // namespace

int ExecuteCefSubprocess(void* module_instance) {
    CefMainArgs args(static_cast<HINSTANCE>(module_instance));
    CefRefPtr<WebViewCefApp> app(new WebViewCefApp());
    return CefExecuteProcess(args, app, nullptr);
}

class WebViewRuntime::Impl {
public:
    bool Start(void* module_instance, const WebViewRuntimeConfig& config,
               WebViewRuntimeCallbacks callbacks, std::string& error) {
        if (started_) {
            error = "CEF is already initialized";
            return false;
        }
        std::string entry_origin;
        url_ = DecodeAndValidateUrl(config.url_b64, entry_origin, error);
        if (url_.empty()) {
            return false;
        }
        config_ = config;
        config_.entry_origin = std::move(entry_origin);
        profile_path_ = MakeProfilePath(config.instance_id);
        std::error_code fs_error;
        // The profile is intentionally ephemeral. Clear a stale directory
        // left by a forced termination before CEF acquires its process lock.
        std::filesystem::remove_all(profile_path_, fs_error);
        fs_error.clear();
        std::filesystem::create_directories(profile_path_, fs_error);
        if (fs_error) {
            error = "cannot create isolated WebView profile";
            return false;
        }

        CefSettings settings{};
        settings.no_sandbox = true;
        settings.multi_threaded_message_loop = true;
        settings.windowless_rendering_enabled = true;
        settings.persist_session_cookies = false;
        // Chromium errors can contain the full page URL. The host reports
        // sanitized error codes itself, so disable CEF's own URL-bearing log.
        settings.log_severity = LOGSEVERITY_DISABLE;
        CefString(&settings.root_cache_path) = profile_path_;
        CefString(&settings.cache_path) = profile_path_;
        CefString(&settings.locale) = "zh-CN";

        app_ = new WebViewCefApp();
        CefMainArgs args(static_cast<HINSTANCE>(module_instance));
        if (!CefInitialize(args, settings, app_, nullptr)) {
            error = "CEF initialization failed";
            app_ = nullptr;
            return false;
        }
        started_ = true;
        client_ = new WebViewClient(config_, std::move(callbacks));

        CefWindowInfo window_info{};
        window_info.SetAsWindowless(nullptr);
        window_info.shared_texture_enabled = config.accelerated_paint;
        CefBrowserSettings browser_settings{};
        browser_settings.windowless_frame_rate = std::clamp(config.frame_rate, 1, 120);
        if (!CefBrowserHost::CreateBrowser(window_info, client_, url_, browser_settings,
                                           nullptr, nullptr)) {
            error = "CEF browser creation failed";
            Stop();
            return false;
        }
        LOGI("WebView CEF initialized: viewport={}x{} fps={} audio={} accelerated={}",
             config.width, config.height, config.frame_rate, config.enable_audio,
             config.accelerated_paint);
        return true;
    }

    void Stop() {
        if (!started_) return;
        if (client_) {
            client_->Close();
            if (!client_->WaitUntilClosed(std::chrono::seconds(5))) {
                LOGW("Timed out waiting for WebView browser close");
            }
        }
        client_ = nullptr;
        CefShutdown();
        app_ = nullptr;
        started_ = false;
        std::error_code ignored;
        std::filesystem::remove_all(profile_path_, ignored);
        LOGI("WebView CEF shutdown complete");
    }

    bool started_ = false;
    std::string url_;
    std::wstring profile_path_;
    WebViewRuntimeConfig config_;
    CefRefPtr<WebViewCefApp> app_;
    CefRefPtr<WebViewClient> client_;
};

WebViewRuntime::WebViewRuntime() : impl_(std::make_unique<Impl>()) {}
WebViewRuntime::~WebViewRuntime() { Stop(); }

bool WebViewRuntime::Start(void* module_instance,
                           const WebViewRuntimeConfig& config,
                           WebViewRuntimeCallbacks callbacks,
                           std::string& error) {
    return impl_->Start(module_instance, config, std::move(callbacks), error);
}

void WebViewRuntime::Stop() { impl_->Stop(); }

void WebViewRuntime::SetActive(bool active) {
    if (impl_->client_) impl_->client_->SetActive(active);
}

void WebViewRuntime::SendMouseEvent(const MouseEvent& event) {
    if (impl_->client_) impl_->client_->SendMouse(event);
}

void WebViewRuntime::SendKeyEvent(const KeyEvent& event) {
    if (impl_->client_) impl_->client_->SendKey(event);
}

void WebViewRuntime::SendTextInput(const TextInput& event) {
    if (impl_->client_) impl_->client_->SendText(event);
}

void WebViewRuntime::SendFocusEvent(bool focused) {
    if (impl_->client_) impl_->client_->SendFocus(focused);
}

} // namespace px
