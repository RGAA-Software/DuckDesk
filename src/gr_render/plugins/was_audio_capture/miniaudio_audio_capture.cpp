#include "miniaudio_audio_capture.h"

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "tc_common_new/log.h"

namespace tc
{

	const char* MiniAudioCapture::ResultStr(ma_result result) {
		const char* desc = ma_result_description(result);
		return desc ? desc : "unknown";
	}

	MiniAudioCapture::MiniAudioCapture(uint32_t loopback_process_id)
		: loopback_process_id_(loopback_process_id) {}

	AudioCapturePtr MiniAudioCapture::Make() {
		auto capture = std::shared_ptr<MiniAudioCapture>(new MiniAudioCapture(0));
		LOGI("[MiniAudioCapture] Make (OS default playback loopback)");
		return capture;
	}

	AudioCapturePtr MiniAudioCapture::MakeForProcess(uint32_t process_id) {
		auto capture = std::shared_ptr<MiniAudioCapture>(new MiniAudioCapture(process_id));
		LOGI("[MiniAudioCapture] MakeForProcess pid={}", process_id);
		return capture;
	}

	MiniAudioCapture::~MiniAudioCapture() {
		want_running_ = false;
		Stop();
	}

	void MiniAudioCapture::CleanupUnlocked() {
		if (device_inited_) {
			if (running_) {
				ma_result r = ma_device_stop(&device_);
				if (r != MA_SUCCESS) {
					LOGW("[MiniAudioCapture] ma_device_stop failed: {} ({})", (int)r, ResultStr(r));
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
			LOGE("[MiniAudioCapture] ma_context_init(WASAPI) failed: {} ({})", (int)result, ResultStr(result));
			return (int)result;
		}
		context_inited_ = true;

		ma_device_config device_config = ma_device_config_init(ma_device_type_loopback);
		device_config.capture.format = ma_format_s16;
		device_config.capture.channels = kChannels;
		device_config.sampleRate = kSampleRate;
		device_config.dataCallback = DataCallback;
		device_config.notificationCallback = NotificationCallback;
		device_config.pUserData = this;
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
				 (int)result, ResultStr(result), loopback_process_id_);
			CleanupUnlocked();
			return (int)result;
		}
		device_inited_ = true;

		LOGI("[MiniAudioCapture] device opened: name=\"{}\", pid={}, format={}Hz/{}ch/{}bit",
			 device_.capture.name[0] ? device_.capture.name : "<unnamed>",
			 loopback_process_id_, kSampleRate, kChannels, kBits);

		if (format_callback_) {
			format_callback_(kSampleRate, kChannels, kBits);
		} else {
			LOGW("[MiniAudioCapture] format_callback_ is null");
		}

		// Allow DataCallback during ma_device_start (WASAPI may deliver first buffers there).
		running_ = true;
		result = ma_device_start(&device_);
		if (result != MA_SUCCESS) {
			running_ = false;
			LOGE("[MiniAudioCapture] ma_device_start failed: {} ({})", (int)result, ResultStr(result));
			CleanupUnlocked();
			return (int)result;
		}

		LOGI("[MiniAudioCapture] started OK ({} {}Hz {}ch {}bit)",
			 loopback_process_id_ ? "PID process-loopback" : "default loopback",
			 kSampleRate, kChannels, kBits);
		return 0;
	}

	int MiniAudioCapture::Start() {
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
			LOGE("[MiniAudioCapture] Pause ma_device_stop failed: {} ({})", (int)r, ResultStr(r));
			return (int)r;
		}
		running_ = false;
		LOGI("[MiniAudioCapture] paused, frames={}, bytes={}, peak={}",
			 total_frames_.load(), total_bytes_.load(), peak_abs_.load());
		if (pause_callback_) {
			pause_callback_();
		}
		return 0;
	}

	int MiniAudioCapture::Stop() {
		want_running_ = false;
		std::lock_guard<std::mutex> lock(lifecycle_mutex_);
		const bool was_active = device_inited_ || context_inited_;
		if (!was_active) {
			return 0;
		}

		LOGI("[MiniAudioCapture] Stop begin, frames={}, bytes={}, peak={}",
			 total_frames_.load(), total_bytes_.load(), peak_abs_.load());
		CleanupUnlocked();
		if (stop_callback_) {
			stop_callback_();
		}
		LOGI("[MiniAudioCapture] stopped");
		return 0;
	}

	void MiniAudioCapture::RequestReinit(const char* reason) {
		if (!want_running_) {
			return;
		}
		if (reinit_pending_.exchange(true)) {
			LOGI("[MiniAudioCapture] reinit already pending, skip ({})", reason ? reason : "");
			return;
		}
		LOGI("[MiniAudioCapture] schedule reinit: {}", reason ? reason : "unknown");
		std::weak_ptr<MiniAudioCapture> weak = shared_from_this();
		std::thread([weak, reason_str = std::string(reason ? reason : "unknown")]() {
			// Leave the WASAPI notification callback before tearing down the device.
			std::this_thread::sleep_for(std::chrono::milliseconds(80));
			if (auto self = weak.lock()) {
				self->ReinitForDefaultDevice(reason_str.c_str());
				self->reinit_pending_ = false;
			}
		}).detach();
	}

	void MiniAudioCapture::ReinitForDefaultDevice(const char* reason) {
		std::lock_guard<std::mutex> lock(lifecycle_mutex_);
		if (!want_running_) {
			LOGI("[MiniAudioCapture] reinit aborted (stopped): {}", reason ? reason : "");
			return;
		}
		LOGW("[MiniAudioCapture] reinit for OS default device, reason={}, old_name=\"{}\"",
			 reason ? reason : "",
			 device_inited_ && device_.capture.name[0] ? device_.capture.name : "<none>");
		CleanupUnlocked();
		const int ret = OpenAndStartUnlocked();
		if (ret != 0) {
			LOGE("[MiniAudioCapture] reinit failed: {}", ret);
		} else {
			LOGI("[MiniAudioCapture] reinit OK, name=\"{}\"",
				 device_.capture.name[0] ? device_.capture.name : "<unnamed>");
		}
	}

	void MiniAudioCapture::NotificationCallback(const ma_device_notification* notification) {
		if (!notification || !notification->pDevice) {
			return;
		}
		auto* self = static_cast<MiniAudioCapture*>(notification->pDevice->pUserData);
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
		auto* self = static_cast<MiniAudioCapture*>(device->pUserData);
		if (!self || !self->running_ || !input || frame_count == 0) {
			return;
		}
		self->EmitPcm(input, frame_count);
	}

	void MiniAudioCapture::EmitPcm(const void* input, ma_uint32 frame_count) {
		const auto bytes_to_write = static_cast<size_t>(frame_count) * kChannels * (kBits / 8);
		if (bytes_to_write == 0) {
			return;
		}

		const auto* samples = static_cast<const int16_t*>(input);
		const size_t sample_count = bytes_to_write / sizeof(int16_t);
		int local_peak = 0;
		for (size_t i = 0; i < sample_count; ++i) {
			const int a = samples[i] < 0 ? -samples[i] : samples[i];
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

		if (data_callback_) {
			auto data = Data::Make((char*)input, (int)bytes_to_write);
			data_callback_(data);
		}

		if (split_data_callback_) {
			auto left_data = Data::Make(nullptr, (int)(bytes_to_write / 2));
			auto right_data = Data::Make(nullptr, (int)(bytes_to_write / 2));
			if (!left_data || !right_data || !left_data->DataAddr() || !right_data->DataAddr()) {
				LOGE("[MiniAudioCapture] split buffer alloc failed, bytes={}", bytes_to_write);
				return;
			}
			const auto* pcm = static_cast<const char*>(input);
			for (size_t i = 0; i < bytes_to_write; i += 4) {
				memcpy(left_data->DataAddr() + i / 4 * 2, pcm + i, 2);
				memcpy(right_data->DataAddr() + i / 4 * 2, pcm + i + 2, 2);
			}
			split_data_callback_(left_data, right_data);
		}
	}

}
