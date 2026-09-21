#pragma once

namespace px::rdp {

class ProcessAudioController final {
public:
    ProcessAudioController();
    ~ProcessAudioController();
    ProcessAudioController(const ProcessAudioController&) = delete;
    ProcessAudioController& operator=(const ProcessAudioController&) = delete;

    [[nodiscard]] bool ApplyMuted(bool muted) const;

private:
    bool comAvailable_{};
    bool ownsComInitialization_{};
};

}  // namespace px::rdp
