#include <iostream>
#include <memory>

#include "../frame_refresh_signal.h"
#include "../video_presentation_progress.h"

int main() {
    using Progress = px::desktop::VideoPresentationProgress;
    using namespace std::chrono_literals;
    const Progress::Clock::time_point started{};
    Progress progress{};
    if (progress.Presented(true, started, 1ms)) return 1;
    progress.Uploaded(started);
    if (progress.Presented(false, started + 1ms, 1ms)) return 2;
    const auto first = progress.Presented(true, started + 2ms, 2ms);
    if (!first || first->generation != 1 || first->gap_us != 0 || first->upload_age_us != 2000) return 3;
    // Hundreds of successful UI repaints cannot fabricate new video progress.
    for (int repaint_index{}; repaint_index < 500; ++repaint_index) {
        if (progress.Presented(true, started + 3ms + repaint_index * 10ms, 1ms)) return 4;
    }
    progress.Uploaded(started + 5s);
    progress.Uploaded(started + 5010ms);
    const auto resumed = progress.Presented(true, started + 5020ms, 10ms);
    if (!resumed || resumed->generation != 3 || resumed->gap_us != 5'018'000 || resumed->upload_age_us != 10'000 || resumed->window_frames != 2 ||
        resumed->window_us != 5'018'000 || resumed->maximum_render_us != 10'000)
        return 5;
    progress.Uploaded(started + 5030ms);
    const auto next = progress.Presented(true, started + 5040ms, 2ms);
    if (!next || next->gap_us != 20'000 || next->window_frames != 0) return 6;
    auto signal = std::make_shared<px::desktop::FrameRefreshSignal>();
    const auto enqueued = std::make_shared<int>();
    const auto wake = [signal = std::weak_ptr{signal}, enqueued] {
        if (const auto active = signal.lock())
            active->Request([enqueued] {
                ++*enqueued;
                return true;
            });
    };
    wake();
    wake();
    if (*enqueued != 1) return 7;
    signal->Consume();
    signal->Request([] { return false; });
    wake();
    if (*enqueued != 2) return 8;
    signal.reset();
    wake();
    if (*enqueued != 2) return 9;
    std::cout << "Failed presentation, duplicate repaint, replaced upload and stalled-video accounting passed\n";
    return 0;
}
