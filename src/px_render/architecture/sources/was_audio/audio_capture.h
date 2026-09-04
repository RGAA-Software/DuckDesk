#pragma once

#include "px_common_new/data.h"

#include <memory>
#include <functional>
#include <mutex>

typedef std::function<void()> OnPrepareCallback;
typedef std::function<void(int samples, int channels, int bits)> OnFormatCallback;
typedef std::function<void(const px::DataPtr& data)> OnDataCallback;
typedef std::function<void(const px::DataPtr& left, const px::DataPtr& right)> OnSplitDataCallback;
typedef std::function<void()> OnPauseCallback;
typedef std::function<void()> OnStopCallback;

namespace px
{

	class IAudioCapture {
	public:
		virtual ~IAudioCapture() = default;

		virtual int Start() = 0;
		virtual int Pause() = 0;
		virtual int Stop() = 0;

		void RegisterPrepareCallback(const OnPrepareCallback& cbk) {
			std::scoped_lock lock(callback_state_->mutex);
            callback_state_->prepare = cbk;
		}

		void RegisterFormatCallback(const OnFormatCallback& cbk) {
			std::scoped_lock lock(callback_state_->mutex);
			callback_state_->format = cbk;
		}

		void RegisterDataCallback(const OnDataCallback& cbk) {
			std::scoped_lock lock(callback_state_->mutex);
			callback_state_->data = cbk;
		}

		void RegisterSplitDataCallback(const OnSplitDataCallback& cbk) {
			std::scoped_lock lock(callback_state_->mutex);
			callback_state_->split_data = cbk;
		}

		void RegisterPauseCallback(const OnPauseCallback& cbk) {
			std::scoped_lock lock(callback_state_->mutex);
			callback_state_->pause = cbk;
		}

		void RegisterStopCallback(const OnStopCallback& cbk) {
			std::scoped_lock lock(callback_state_->mutex);
			callback_state_->stop = cbk;
		}

		// True when the capture thread itself terminated on a fatal device error
		// (e.g. AUDCLNT_E_DEVICE_INVALIDATED), as opposed to a normal Stop().
		// Queried from the stop callback to decide on auto-restart.
		virtual bool IsFatalStop() const { return false; }

	protected:
		struct CallbackSnapshot final {
			OnPrepareCallback prepare;
			OnFormatCallback format;
			OnDataCallback data;
			OnSplitDataCallback split_data;
			OnPauseCallback pause;
			OnStopCallback stop;
		};

		struct CallbackState final {
			std::mutex mutex;
			OnPrepareCallback prepare;
			OnFormatCallback format;
			OnDataCallback data;
			OnSplitDataCallback split_data;
			OnPauseCallback pause;
			OnStopCallback stop;
		};

		[[nodiscard]] static CallbackSnapshot SnapshotCallbacks(
			const std::shared_ptr<CallbackState>& state) {
			std::scoped_lock lock(state->mutex);
			return CallbackSnapshot{
				.prepare = state->prepare,
				.format = state->format,
				.data = state->data,
				.split_data = state->split_data,
				.pause = state->pause,
				.stop = state->stop,
			};
		}

		[[nodiscard]] CallbackSnapshot SnapshotCallbacks() const {
			return SnapshotCallbacks(callback_state_);
		}

		std::shared_ptr<CallbackState> callback_state_ =
			std::make_shared<CallbackState>();
        std::string device_id_;
	};

	typedef std::shared_ptr<IAudioCapture> AudioCapturePtr;
}
