#pragma once

#include "audio_capture.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>

#include "third_party/miniaudio/miniaudio.h"

namespace px
{

	class MiniAudioCapture : public IAudioCapture,
	                         public std::enable_shared_from_this<MiniAudioCapture> {
	private:
		struct ConstructionToken final {};

	public:
		// Desktop: OS default playback device loopback (mixed system audio).
		static AudioCapturePtr Make();
		// Game-hook: WASAPI process-loopback for a single PID (multi-instance safe).
		static AudioCapturePtr MakeForProcess(uint32_t process_id);
		explicit MiniAudioCapture(
			ConstructionToken, uint32_t loopback_process_id);

		~MiniAudioCapture();

		int Start() override;
		int Pause() override;
		int Stop() override;

#if defined(PX_AUDIO_CAPTURE_TESTING)
		void RequestReinitForTesting(const std::string& reason) {
			RequestReinit(reason);
		}
		[[nodiscard]] uint64_t SuccessfulReinitCountForTesting() const {
			return successful_reinit_count_.load();
		}
#endif

	private:
		struct CallbackBridge final {
			std::weak_ptr<MiniAudioCapture> owner;
			std::atomic_bool accepting = false;
		};

		void InitializeCallbackBridge();
		// NOLINTNEXTLINE(gammaray-raw-pointer-boundary): miniaudio callback ABI
		static void DataCallback(ma_device* device, void* output, const void* input, ma_uint32 frame_count);
		// NOLINTNEXTLINE(gammaray-raw-pointer-boundary): miniaudio callback ABI
		static void NotificationCallback(const ma_device_notification* notification);

		int OpenAndStartUnlocked();
		void CleanupUnlocked();
		void RequestReinit(std::string reason);
		void ReinitForDefaultDevice(const std::string& reason);
		void EnsureReinitWorker();
		void StopReinitWorker();
		void EmitPcm(std::span<const int16_t> input, ma_uint32 frame_count);
		static std::string ResultText(ma_result result);

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
		std::atomic<uint64_t> successful_reinit_count_{0};
		std::atomic<int> peak_abs_{0};
		std::mutex operation_mutex_;
		std::mutex lifecycle_mutex_;
		struct ReinitWorkerState final {
			std::mutex mutex;
			std::condition_variable_any condition;
			bool pending = false;
			std::string reason;
		};
		std::shared_ptr<ReinitWorkerState> reinit_worker_state_ =
			std::make_shared<ReinitWorkerState>();
		std::shared_ptr<CallbackBridge> callback_bridge_;
		std::mutex reinit_worker_mutex_;
		std::jthread reinit_worker_;
		uint32_t loopback_process_id_ = 0;

		static constexpr int kSampleRate = 48000;
		static constexpr int kChannels = 2;
		static constexpr int kBits = 16;
	};

}
