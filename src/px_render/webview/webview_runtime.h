#ifndef PX_WEBVIEW_RUNTIME_H
#define PX_WEBVIEW_RUNTIME_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "ingress/application_text_service.h"
#include "px_capture/capture_message.h"
#include "px_message.pb.h"

namespace px {

using WebViewTextTarget = ApplicationTextBackendState;

struct WebViewRuntimeConfig {
    std::string url_b64;
    // Populated internally after URL validation; never logged.
    std::string entry_origin;
    std::string instance_id;
    int width = 1920;
    int height = 1080;
    int frame_rate = 60;
    bool enable_audio = true;
    bool accelerated_paint = true;
    std::string gpu_stable_key;
};

struct WebViewRuntimeCallbacks {
    std::function<void(const CaptureVideoFrame&)> on_video_frame;
    std::function<void(const CaptureAudioFrame&)> on_audio_frame;
    std::function<void(const CaptureCursorBitmap&)> on_cursor;
    std::function<void(const std::string&)> on_clipboard_text;
    std::function<void(const WebViewTextTarget&)> on_text_target;
    std::function<void(const std::string&)> on_failed;
    std::function<void(std::int64_t)> on_first_frame;
};

// Must be called before gflags or any application initialization. Returns a
// non-negative process exit code for CEF renderer/GPU/utility children and -1
// in the browser (root px_render) process.
int ExecuteCefSubprocess(std::uintptr_t module_instance);

class WebViewRuntime {
public:
    WebViewRuntime();
    ~WebViewRuntime();

    WebViewRuntime(const WebViewRuntime&) = delete;
    WebViewRuntime& operator=(const WebViewRuntime&) = delete;

    bool Start(std::uintptr_t module_instance, const WebViewRuntimeConfig& config, WebViewRuntimeCallbacks callbacks, std::string& error);
    void Stop();

    void SetActive(bool active);
    void RequestFrame();
    void SendMouseEvent(const MouseEvent& event);
    void SendKeyEvent(const KeyEvent& event);
    void SendTextInput(const TextInput& event);
    void SendFocusEvent(bool focused);
    void SetClipboardText(std::string text);
    void QueryTextTarget(std::function<void(WebViewTextTarget)> completion);
    void ReleaseTextInputKeys(std::function<void()> completion);
    void CommitApplicationText(std::string text, std::string expected_generation, std::function<bool()> authorize,
                               std::function<void(ApplicationTextOutcome)> completion);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace px

#endif  // PX_WEBVIEW_RUNTIME_H
