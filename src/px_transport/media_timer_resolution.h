#pragma once

namespace px::transport {

// Windows timer resolution is process-scoped. The media sender cannot rely on a game or the retired UDP module owning it.
class MediaTimerResolution final {
public:
    MediaTimerResolution();
    ~MediaTimerResolution();
    MediaTimerResolution(const MediaTimerResolution&) = delete;
    MediaTimerResolution& operator=(const MediaTimerResolution&) = delete;

private:
    bool active_{};
};

}  // namespace px::transport
