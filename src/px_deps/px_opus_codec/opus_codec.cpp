// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include <iostream>

#include "opus_codec.h"
#include "px_common/log.h"

namespace px
{

	std::string ErrorToString(int error) {
		switch (error) {
		case OPUS_OK:
			return "OK";
		case OPUS_BAD_ARG:
			return "One or more invalid/out of range arguments.";
		case OPUS_BUFFER_TOO_SMALL:
			return "The mode struct passed is invalid.";
		case OPUS_INTERNAL_ERROR:
			return "An internal error was detected.";
		case OPUS_INVALID_PACKET:
			return "The compressed data passed is corrupted.";
		case OPUS_UNIMPLEMENTED:
			return "Invalid/unsupported request number.";
		case OPUS_INVALID_STATE:
			return "An encoder or decoder structure is invalid or already freed.";
		default:
			return "Unknown error code: " + std::to_string(error);
		}
	}

	void internal::OpusDestroyer::operator()(OpusEncoder* encoder) const
		noexcept {
		opus_encoder_destroy(encoder);
	}

	void internal::OpusDestroyer::operator()(OpusDecoder* decoder) const
		noexcept {
		opus_decoder_destroy(decoder);
	}

    OpusAudioEncoder::OpusAudioEncoder(opus_int32 sample_rate, int num_channels, int bits,
		int application, int expected_loss_percent)
		: sample_rate_{sample_rate}, num_channels_{ num_channels }, bits_{bits} {
		int error{};
		encoder_.reset(
			opus_encoder_create(sample_rate, num_channels, application, &error));
		valid_ = error == OPUS_OK;
		if (!valid()) {
			std::cout << "Could not construct encoder. Error: " << ErrorToString(error) << std::endl;
			return;
		}
		if (expected_loss_percent > 0) {
			std::cout << "Enabling FEC in the encoder." << std::endl;
			Ctl(OPUS_SET_INBAND_FEC(1));
			Ctl(OPUS_SET_PACKET_LOSS_PERC(expected_loss_percent));
		}
	}

	bool OpusAudioEncoder::ResetState() {
		valid_ = Ctl(OPUS_RESET_STATE) == OPUS_OK;
		return valid_;
	}

	bool OpusAudioEncoder::SetBitrate(int bitrate) {
		valid_ = Ctl(OPUS_SET_BITRATE(bitrate)) == OPUS_OK;
		return valid_;
	}

	bool OpusAudioEncoder::SetVariableBitrate(int vbr) {
		valid_ = Ctl(OPUS_SET_VBR(vbr)) == OPUS_OK;
		return valid_;
	}

	bool OpusAudioEncoder::SetComplexity(int complexity) {
		valid_ = Ctl(OPUS_SET_COMPLEXITY(complexity)) == OPUS_OK;
		return valid_;
	}

	int OpusAudioEncoder::GetLookahead() {
		opus_int32 skip{};
		valid_ = Ctl(OPUS_GET_LOOKAHEAD(&skip)) == OPUS_OK;
		return skip;
	}

	std::vector<std::vector<unsigned char>> OpusAudioEncoder::Encode(std::span<const opus_int16> pcm, int frame_size) {
		if (!valid_ || frame_size <= 0 || num_channels_ <= 0) {
			return {};
		}

		const auto samples_per_frame = static_cast<std::size_t>(frame_size) * static_cast<std::size_t>(num_channels_);
		auto sample_count = pcm.size();
		if (sample_count % samples_per_frame != 0u) {
			std::cout << "PCM samples contain an incomplete frame. Ignoring the "
				"incomplete frame." << std::endl;
			sample_count -= sample_count % samples_per_frame;
		}

		std::vector<std::vector<unsigned char>> encoded;
		encoded.reserve(sample_count / samples_per_frame);
		for (std::size_t offset{}; offset < sample_count; offset += samples_per_frame) {
			encoded.push_back(EncodeFrame(pcm.subspan(offset, samples_per_frame), frame_size));
		}
		return encoded;
	}

	std::vector<std::vector<unsigned char>> OpusAudioEncoder::Encode(std::span<const std::byte> pcm, int frame_size) {
		if (pcm.size_bytes() % sizeof(opus_int16) != 0u) {
			std::cout << "PCM byte count is not aligned to 16-bit samples." << std::endl;
			return {};
		}

		std::vector<opus_int16> audio_data(pcm.size_bytes() / sizeof(opus_int16));
		std::memcpy(audio_data.data(), pcm.data(), pcm.size_bytes());
		return Encode(audio_data, frame_size);
	}

	std::vector<unsigned char> OpusAudioEncoder::EncodeFrame(std::span<const opus_int16> frame, int frame_size) {
		const auto frame_length = static_cast<std::size_t>(frame_size) * static_cast<std::size_t>(num_channels_) * sizeof(opus_int16);
		std::vector<unsigned char> encoded(frame_length);
		const auto num_bytes = opus_encode(encoder_.get(), frame.data(), frame_size, encoded.data(), static_cast<opus_int32>(encoded.size()));
		if (num_bytes < 0) {
			std::cout << "Encode error: " << ErrorToString(num_bytes) << std::endl;
			return {};
		}
		encoded.resize(num_bytes);
		return encoded;
	}

    //
    OpusAudioDecoder::OpusAudioDecoder(opus_uint32 sample_rate, int num_channels)
		: num_channels_(num_channels) {
		int error{};
		decoder_.reset(opus_decoder_create(sample_rate, num_channels, &error));
		valid_ = error == OPUS_OK;
	}

	std::vector<opus_int16> OpusAudioDecoder::Decode(
		const std::vector<std::vector<unsigned char>>& packets, int frame_size,
		bool decode_fec) {
		std::vector<opus_int16> decoded;
		for (const auto& enc : packets) {
			auto just_decoded = Decode(enc, frame_size, decode_fec);
			decoded.insert(std::end(decoded), std::begin(just_decoded),
				std::end(just_decoded));
		}
		return decoded;
	}

	std::vector<opus_int16> OpusAudioDecoder::Decode(
		const std::vector<unsigned char>& packet, int frame_size, bool decode_fec) {
		const auto frame_length = static_cast<std::size_t>(frame_size) * static_cast<std::size_t>(num_channels_);
		std::vector<opus_int16> decoded(frame_length);
		auto num_samples = opus_decode(decoder_.get(), packet.data(), packet.size(),
			decoded.data(), frame_size, decode_fec);
		if (num_samples < 0) {
			std::cout << "Decode error: " << ErrorToString(num_samples) << std::endl;
			return {};
		}
		decoded.resize(num_samples * num_channels_);
		return decoded;
	}

	std::vector<opus_int16> OpusAudioDecoder::DecodeDummy(int frame_size) {
		const auto frame_length = static_cast<std::size_t>(frame_size) * static_cast<std::size_t>(num_channels_);
		std::vector<opus_int16> decoded(frame_length);
		auto num_samples =
			opus_decode(decoder_.get(), nullptr, 0, decoded.data(), frame_size, true);
		if (num_samples < 0) {
			std::cout << "Decode error: " << ErrorToString(num_samples) << std::endl;
			return {};
		}
		decoded.resize(num_samples * num_channels_);
		return decoded;
	}

}
