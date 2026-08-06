#pragma once

#include "audio_capture.h"

#include <atomic>
#include <memory>
#include <mutex>

#include "third_party/miniaudio/miniaudio.h"

namespace tc
{

	class MiniAudioCapture : public IAudioCapture,
	                         public std::enable_shared_from_this<MiniAudioCapture> {
	public:
		// Always captures the OS default playback device (WASAPI loopback).
		static AudioCapturePtr Make();

		~MiniAudioCapture();

		int Start() override;
		int Pause() override;
		int Stop() override;

	private:
		static void DataCallback(ma_device* device, void* output, const void* input, ma_uint32 frame_count);
		static void NotificationCallback(const ma_device_notification* notification);

		int OpenAndStartUnlocked();
		void CleanupUnlocked();
		void RequestReinit(const char* reason);
		void ReinitForDefaultDevice(const char* reason);
		void EmitPcm(const void* input, ma_uint32 frame_count);
		static const char* ResultStr(ma_result result);

		ma_context context_{};
		ma_device device_{};
		bool context_inited_ = false;
		bool device_inited_ = false;
		std::atomic<bool> running_{false};
		std::atomic<bool> want_running_{false};
		std::atomic<bool> reinit_pending_{false};
		std::atomic<bool> first_data_logged_{false};
		std::atomic<uint64_t> total_frames_{0};
		std::atomic<uint64_t> total_bytes_{0};
		std::atomic<int> peak_abs_{0};
		std::mutex lifecycle_mutex_;

		static constexpr int kSampleRate = 48000;
		static constexpr int kChannels = 2;
		static constexpr int kBits = 16;
	};

}
