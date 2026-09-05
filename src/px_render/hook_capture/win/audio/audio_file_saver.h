#pragma once

#include <string>
#include <memory>
#include <span>
#include <Windows.h>
#include <mmeapi.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

namespace px
{

	class IAudioFileSaver {
	public:

		virtual int WriteData(std::span<const char> data) = 0;
		virtual void Close() = 0;

	};

	typedef std::shared_ptr<IAudioFileSaver> AudioFileSaverPtr;


	class WAVAudioFileSaver : public IAudioFileSaver {
	public:
		explicit WAVAudioFileSaver(const std::wstring& path);
		~WAVAudioFileSaver();

		int WriteData(std::span<const char> data) override;
		void Close() override;

		HRESULT WriteWaveHeader(LPCWAVEFORMATEX pwfx, MMCKINFO* pckRIFF, MMCKINFO* pckData);
		HRESULT FinishWaveFile(MMCKINFO* pckRIFF, MMCKINFO* pckData);

	private:

		HMMIO file_ = nullptr;

	};

	typedef std::shared_ptr<WAVAudioFileSaver> WAVAudioFileSaverPtr;

}
