#include "miniaudio_audio_capture.h"

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "px_common_new/log.h"

namespace px
{

	std::string MiniAudioCapture::ResultText(ma_result result) {
		const char* description = ma_result_description(result); // NOLINT(gammaray-raw-pointer-boundary): borrowed miniaudio result text
		return description ? std::string(description) : std::string("unknown");
	}

	MiniAudioCapture::MiniAudioCapture(
		ConstructionToken, uint32_t loopback_process_id)
		: loopback_process_id_(loopback_process_id) {}

	void MiniAudioCapture::InitializeCallbackBridge() {
		callback_bridge_ = std::make_shared<CallbackBridge>();
		callback_bridge_->owner = weak_from_this();
	}

	AudioCapturePtr MiniAudioCapture::Make() {
		auto capture = std::make_shared<MiniAudioCapture>(ConstructionToken{}, 0);
		capture->InitializeCallbackBridge();
		LOGI("[MiniAudioCapture] Make (OS default playback loopback)");
		return capture;
	}

	AudioCapturePtr MiniAudioCapture::MakeForProcess(uint32_t process_id) {
		auto capture = std::make_shared<MiniAudioCapture>(
			ConstructionToken{}, process_id);
		capture->InitializeCallbackBridge();
		LOGI("[MiniAudioCapture] MakeForProcess pid={}", process_id);
		return capture;
	}

	MiniAudioCapture::~MiniAudioCapture() {
		want_running_ = false;
		Stop();
		callback_bridge_.reset();
	}

	void MiniAudioCapture::EnsureReinitWorker() {
		std::lock_guard worker_lock(reinit_worker_mutex_);
		if (reinit_worker_.joinable()) {
			return;
		}
		const auto state = reinit_worker_state_;
		const auto weak_self = weak_from_this();
		reinit_worker_ = std::jthread(
			[state, weak_self](std::stop_token stop_token) {
				std::unique_lock lock(state->mutex);
				while (!stop_token.stop_requested()) {
					state->condition.wait(lock, stop_token, [state]() {
						return state->pending;
					});
					if (stop_token.stop_requested()) {
						break;
					}
					const auto reason = state->reason;
					// Leave the WASAPI notification callback before tearing
					// down the device. Stop() cancels this wait immediately.
					static_cast<void>(state->condition.wait_for(
						lock, stop_token, std::chrono::milliseconds(80), []() {
							return false;
						}));
					if (stop_token.stop_requested()) {
						break;
					}
					state->pending = false;
					lock.unlock();
					if (const auto self = weak_self.lock()) {
						self->ReinitForDefaultDevice(reason);
						self->reinit_pending_ = false;
					}
					lock.lock();
				}
			});
	}

	void MiniAudioCapture::StopReinitWorker() {
		std::jthread worker;
		{
			std::lock_guard worker_lock(reinit_worker_mutex_);
			worker = std::move(reinit_worker_);
		}
		if (worker.joinable()) {
			worker.request_stop();
			reinit_worker_state_->condition.notify_all();
			worker.join();
		}
		{
			std::lock_guard state_lock(reinit_worker_state_->mutex);
			reinit_worker_state_->pending = false;
			reinit_worker_state_->reason.clear();
		}
		reinit_pending_ = false;
	}

	void MiniAudioCapture::CleanupUnlocked() {
		if (callback_bridge_) {
			callback_bridge_->accepting = false;
		}
		if (device_inited_) {
			if (running_) {
				ma_result r = ma_device_stop(&device_);
				if (r != MA_SUCCESS) {
					LOGW("[MiniAudioCapture] ma_device_stop failed: {} ({})", (int)r, ResultText(r));
				}
				running_ = false;
			}
			ma_device_uninit(&device_);
			device_inited_ = false;
		}
		if (context_inited_) {
			ma_context_uninit(&context_);
			context_inited_ = false;
		}
	}

	int MiniAudioCapture::OpenAndStartUnlocked() {
		first_data_logged_ = false;
		total_frames_ = 0;
		total_bytes_ = 0;
		peak_abs_ = 0;

		ma_backend backends[] = { ma_backend_wasapi };
		ma_context_config context_config = ma_context_config_init();
		ma_result result = ma_context_init(backends, 1, &context_config, &context_);
		if (result != MA_SUCCESS) {
			LOGE("[MiniAudioCapture] ma_context_init(WASAPI) failed: {} ({})", (int)result, ResultText(result));
			return (int)result;
		}
		context_inited_ = true;

		ma_device_config device_config = ma_device_config_init(ma_device_type_loopback);
		device_config.capture.format = ma_format_s16;
		device_config.capture.channels = kChannels;
		device_config.sampleRate = kSampleRate;
		device_config.dataCallback = DataCallback;
		device_config.notificationCallback = NotificationCallback;
		const auto callback_bridge = callback_bridge_;
		if (!callback_bridge) {
			LOGE("[MiniAudioCapture] callback bridge is unavailable");
			CleanupUnlocked();
			return MA_INVALID_OPERATION;
		}
		callback_bridge->accepting = true;
		device_config.pUserData = callback_bridge.get(); // NOLINT(gammaray-raw-pointer-boundary): miniaudio retains userdata only until device uninit
		// nullptr => current OS default render endpoint (or process-loopback virtual device).
		device_config.capture.pDeviceID = nullptr;
		if (loopback_process_id_ != 0) {
			// Per-PID capture for multi-instance game-hook. Do NOT fall back to device mix.
			device_config.wasapi.loopbackProcessID = loopback_process_id_;
			device_config.wasapi.loopbackProcessExclude = MA_FALSE;
			device_config.wasapi.noAutoStreamRouting = MA_TRUE;
		} else {
			// Desktop: allow WASAPI auto stream routing when default device changes.
			device_config.wasapi.noAutoStreamRouting = MA_FALSE;
		}

		result = ma_device_init(&context_, &device_config, &device_);
		if (result != MA_SUCCESS) {
			LOGE("[MiniAudioCapture] ma_device_init(loopback{}) failed: {} ({}) pid={}",
				 loopback_process_id_ ? " process" : " default",
				 (int)result, ResultText(result), loopback_process_id_);
			callback_bridge->accepting = false;
			CleanupUnlocked();
			return (int)result;
		}
		device_inited_ = true;

		LOGI("[MiniAudioCapture] device opened: name=\"{}\", pid={}, format={}Hz/{}ch/{}bit",
			 device_.capture.name[0] ? device_.capture.name : "<unnamed>",
			 loopback_process_id_, kSampleRate, kChannels, kBits);

		const auto callbacks = SnapshotCallbacks();
		if (callbacks.format) {
			callbacks.format(kSampleRate, kChannels, kBits);
		} else {
			LOGW("[MiniAudioCapture] format_callback_ is null");
		}

		// Allow DataCallback during ma_device_start (WASAPI may deliver first buffers there).
		running_ = true;
		result = ma_device_start(&device_);
		if (result != MA_SUCCESS) {
			running_ = false;
			LOGE("[MiniAudioCapture] ma_device_start failed: {} ({})", (int)result, ResultText(result));
			callback_bridge->accepting = false;
			CleanupUnlocked();
			return (int)result;
		}

		LOGI("[MiniAudioCapture] started OK ({} {}Hz {}ch {}bit)",
			 loopback_process_id_ ? "PID process-loopback" : "default loopback",
			 kSampleRate, kChannels, kBits);
		return 0;
	}

	int MiniAudioCapture::Start() {
		std::lock_guard operation_lock(operation_mutex_);
		EnsureReinitWorker();
		std::lock_guard<std::mutex> lock(lifecycle_mutex_);
		want_running_ = true;
		if (running_) {
			LOGW("[MiniAudioCapture] Start ignored: already running");
			return 0;
		}
		if (device_inited_ || context_inited_) {
			LOGW("[MiniAudioCapture] Start: cleaning leftover device/context state");
			CleanupUnlocked();
		}
		LOGI("[MiniAudioCapture] Start begin ({})",
			 loopback_process_id_ ? "PID process-loopback" : "OS default playback loopback");
		return OpenAndStartUnlocked();
	}

	int MiniAudioCapture::Pause() {
		std::lock_guard<std::mutex> lock(lifecycle_mutex_);
		if (!device_inited_ || !running_) {
			LOGI("[MiniAudioCapture] Pause ignored: not running");
			return 0;
		}
		ma_result r = ma_device_stop(&device_);
		if (r != MA_SUCCESS) {
			LOGE("[MiniAudioCapture] Pause ma_device_stop failed: {} ({})", (int)r, ResultText(r));
			return (int)r;
		}
		running_ = false;
		LOGI("[MiniAudioCapture] paused, frames={}, bytes={}, peak={}",
			 total_frames_.load(), total_bytes_.load(), peak_abs_.load());
		const auto callback = SnapshotCallbacks().pause;
		if (callback) {
			callback();
		}
		return 0;
	}

	int MiniAudioCapture::Stop() {
		std::lock_guard operation_lock(operation_mutex_);
		want_running_ = false;
		StopReinitWorker();
		std::lock_guard<std::mutex> lock(lifecycle_mutex_);
		const bool was_active = device_inited_ || context_inited_;
		if (!was_active) {
			return 0;
		}

		LOGI("[MiniAudioCapture] Stop begin, frames={}, bytes={}, peak={}",
			 total_frames_.load(), total_bytes_.load(), peak_abs_.load());
		if (callback_bridge_) {
			callback_bridge_->accepting = false;
		}
		CleanupUnlocked();
		const auto callback = SnapshotCallbacks().stop;
		if (callback) {
			callback();
		}
		LOGI("[MiniAudioCapture] stopped");
		return 0;
	}

	void MiniAudioCapture::RequestReinit(std::string reason) {
		if (!want_running_) {
			return;
		}
		if (reinit_pending_.exchange(true)) {
			LOGI("[MiniAudioCapture] reinit already pending, skip ({})", reason);
			return;
		}
		if (reason.empty()) {
			reason = "unknown";
		}
		LOGI("[MiniAudioCapture] schedule reinit: {}", reason);
		{
			std::lock_guard lock(reinit_worker_state_->mutex);
			reinit_worker_state_->reason = std::move(reason);
			reinit_worker_state_->pending = true;
		}
		reinit_worker_state_->condition.notify_all();
	}

	void MiniAudioCapture::ReinitForDefaultDevice(const std::string& reason) {
		std::lock_guard<std::mutex> lock(lifecycle_mutex_);
		if (!want_running_) {
			LOGI("[MiniAudioCapture] reinit aborted (stopped): {}", reason);
			return;
		}
		LOGW("[MiniAudioCapture] reinit for OS default device, reason={}, old_name=\"{}\"",
			 reason,
			 device_inited_ && device_.capture.name[0] ? device_.capture.name : "<none>");
		CleanupUnlocked();
		const int ret = OpenAndStartUnlocked();
		if (ret != 0) {
			LOGE("[MiniAudioCapture] reinit failed: {}", ret);
		} else {
			++successful_reinit_count_;
			LOGI("[MiniAudioCapture] reinit OK, name=\"{}\"",
				 device_.capture.name[0] ? device_.capture.name : "<unnamed>");
		}
	}

	void MiniAudioCapture::NotificationCallback(const ma_device_notification* notification) {
		if (!notification || !notification->pDevice ||
			!notification->pDevice->pUserData) {
			return;
		}
		auto& bridge = *static_cast<CallbackBridge*>(
			notification->pDevice->pUserData); // NOLINT(gammaray-raw-pointer-boundary): miniaudio userdata boundary
		const auto self = bridge.accepting ? bridge.owner.lock() : nullptr;
		if (!self) {
			return;
		}

		const char* type_name = "unknown";
		switch (notification->type) {
			case ma_device_notification_type_started: type_name = "started"; break;
			case ma_device_notification_type_stopped: type_name = "stopped"; break;
			case ma_device_notification_type_rerouted: type_name = "rerouted"; break;
			case ma_device_notification_type_interruption_began: type_name = "interruption_began"; break;
			case ma_device_notification_type_interruption_ended: type_name = "interruption_ended"; break;
			case ma_device_notification_type_unlocked: type_name = "unlocked"; break;
			default: break;
		}

		if (notification->type == ma_device_notification_type_rerouted) {
			LOGW("[MiniAudioCapture] device notification: {} name=\"{}\" -> reinit default",
				 type_name,
				 notification->pDevice->capture.name);
			self->RequestReinit("rerouted");
		} else if (notification->type == ma_device_notification_type_interruption_ended) {
			LOGW("[MiniAudioCapture] device notification: {} -> reinit default", type_name);
			self->RequestReinit("interruption_ended");
		} else if (notification->type == ma_device_notification_type_interruption_began) {
			LOGW("[MiniAudioCapture] device notification: {}", type_name);
		} else {
			LOGI("[MiniAudioCapture] device notification: {}", type_name);
		}
	}

	void MiniAudioCapture::DataCallback(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {
		(void)output;
		if (!device || !device->pUserData || !input || frame_count == 0) {
			return;
		}
		auto& bridge = *static_cast<CallbackBridge*>(
			device->pUserData); // NOLINT(gammaray-raw-pointer-boundary): miniaudio userdata boundary
		const auto self = bridge.accepting ? bridge.owner.lock() : nullptr;
		if (!self || !self->running_) {
			return;
		}
		self->EmitPcm(
			std::span<const int16_t>(
				static_cast<const int16_t*>(input), // NOLINT(gammaray-raw-pointer-boundary): miniaudio PCM buffer boundary
				static_cast<size_t>(frame_count) * kChannels),
			frame_count);
	}

	void MiniAudioCapture::EmitPcm(
		std::span<const int16_t> input, ma_uint32 frame_count) {
		const auto bytes_to_write = static_cast<size_t>(frame_count) * kChannels * (kBits / 8);
		if (bytes_to_write == 0) {
			return;
		}

		int local_peak = 0;
		for (const auto sample : input) {
			const int a = sample < 0 ? -sample : sample;
			if (a > local_peak) {
				local_peak = a;
			}
		}

		total_frames_ += frame_count;
		total_bytes_ += bytes_to_write;
		int prev_peak = peak_abs_.load();
		while (local_peak > prev_peak && !peak_abs_.compare_exchange_weak(prev_peak, local_peak)) {
		}

		if (!first_data_logged_.exchange(true)) {
			LOGI("[MiniAudioCapture] first PCM callback: frames={}, bytes={}, peak={}",
				 frame_count, bytes_to_write, local_peak);
		}

		const auto callbacks = SnapshotCallbacks();
		if (callbacks.data) {
			auto data = Data::Make(
				reinterpret_cast<const char*>(input.data()), bytes_to_write);
			callbacks.data(data);
		}

		if (callbacks.split_data) {
			auto left_data = Data::Make(nullptr, (int)(bytes_to_write / 2));
			auto right_data = Data::Make(nullptr, (int)(bytes_to_write / 2));
			if (!left_data || !right_data || !left_data->DataAddr() || !right_data->DataAddr()) {
				LOGE("[MiniAudioCapture] split buffer alloc failed, bytes={}", bytes_to_write);
				return;
			}
			const auto pcm = std::as_bytes(input);
			for (size_t i = 0; i < bytes_to_write; i += 4) {
				memcpy(left_data->DataAddr() + i / 4 * 2, pcm.data() + i, 2);
				memcpy(right_data->DataAddr() + i / 4 * 2, pcm.data() + i + 2, 2);
			}
			callbacks.split_data(left_data, right_data);
		}
	}

}
