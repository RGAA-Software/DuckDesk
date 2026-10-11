#include "video_presentation_progress.h"

#include "px_common/log.h"

namespace px::desktop {

void LogVideoPresentation(const std::optional<VideoPresentationProgress::Observation>& observation, std::string_view backend) {
    if (!observation) return;
    if (observation->gap_us > 100'000 || observation->upload_age_us > 50'000) {
        LOGI("event=video.present phase=gap backend={} generation={} gap_us={} upload_age_us={}", backend, observation->generation,
             observation->gap_us, observation->upload_age_us);
    }
    if (observation->window_us > 0) {
        LOGI(
            "event=video.present phase=window backend={} frames={} elapsed_us={} fps={:.2f} maximum_gap_us={} maximum_upload_age_us={} "
            "maximum_render_us={} log_overruns={}",
            backend, observation->window_frames, observation->window_us,
            static_cast<double>(observation->window_frames) * 1'000'000 / observation->window_us, observation->maximum_gap_us,
            observation->maximum_upload_age_us, observation->maximum_render_us, Logger::DroppedMediaLogs());
    }
}

}  // namespace px::desktop
