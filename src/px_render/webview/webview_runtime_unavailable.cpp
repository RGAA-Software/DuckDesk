#include <utility>

#include "webview_runtime.h"

namespace px {

class WebViewRuntime::Impl {};

int ExecuteCefSubprocess(std::uintptr_t) { return -1; }

WebViewRuntime::WebViewRuntime() = default;
WebViewRuntime::~WebViewRuntime() = default;

bool WebViewRuntime::Start(std::uintptr_t, const WebViewRuntimeConfig&, WebViewRuntimeCallbacks, std::string& error) {
    error = "WebView host capability is not included in this product";
    return false;
}

void WebViewRuntime::Stop() {}
void WebViewRuntime::SetActive(bool) {}
void WebViewRuntime::SendMouseEvent(const MouseEvent&) {}
void WebViewRuntime::SendKeyEvent(const KeyEvent&) {}
void WebViewRuntime::SendTextInput(const TextInput&) {}
void WebViewRuntime::SendFocusEvent(bool) {}
void WebViewRuntime::SetClipboardText(std::string) {}

void WebViewRuntime::QueryTextTarget(std::function<void(WebViewTextTarget)> completion) {
    if (completion) {
        completion({});
    }
}

void WebViewRuntime::ReleaseTextInputKeys(std::function<void()> completion) {
    if (completion) {
        completion();
    }
}

void WebViewRuntime::CommitApplicationText(std::string, std::string, std::function<bool()>, std::function<void(ApplicationTextOutcome)> completion) {
    if (completion) {
        completion(TEXT_TARGET_UNAVAILABLE);
    }
}

}  // namespace px
