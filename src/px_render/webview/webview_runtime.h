#ifndef PX_WEBVIEW_RUNTIME_H
#define PX_WEBVIEW_RUNTIME_H

#include <functional>
#include <memory>
#include <string>

#include "px_capture_new/capture_message.h"
#include "px_message.pb.h"

namespace px {

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
};

struct WebViewRuntimeCallbacks {
    std::function<void(const CaptureVideoFrame&)> on_video_frame;
    std::function<void(const CaptureAudioFrame&)> on_audio_frame;
    std::function<void(const CaptureCursorBitmap&)> on_cursor;
    std::function<void(const std::string&)> on_failed;
    std::function<void()> on_first_frame;
};

// Must be called before gflags or any application initialization. Returns a
// non-negative process exit code for CEF renderer/GPU/utility children and -1
// in the browser (root px_render) process.
int ExecuteCefSubprocess(void* module_instance);

class WebViewRuntime {
public:
    WebViewRuntime();
    ~WebViewRuntime();

    WebViewRuntime(const WebViewRuntime&) = delete;
    WebViewRuntime& operator=(const WebViewRuntime&) = delete;

    bool Start(void* module_instance,
               const WebViewRuntimeConfig& config,
               WebViewRuntimeCallbacks callbacks,
               std::string& error);
    void Stop();

    void SetActive(bool active);
    void SendMouseEvent(const MouseEvent& event);
    void SendKeyEvent(const KeyEvent& event);
    void SendTextInput(const TextInput& event);
    void SendFocusEvent(bool focused);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace px

#endif // PX_WEBVIEW_RUNTIME_H
