#pragma once

#include <cstdint>
#include <string>

namespace tc {

// Starts tc_audio_pid_capture.exe to record |pid| into |wav_path| via
// external process-loopback (self-loopback inside the game is silent).
// |out_process| receives an OS process handle (void*) or nullptr.
bool StartPidCaptureHelper(uint32_t pid, const std::wstring& wav_path, void** out_process);
void StopPidCaptureHelper(void* process);

}  // namespace tc
