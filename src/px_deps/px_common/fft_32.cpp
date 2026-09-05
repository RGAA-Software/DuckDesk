#ifdef WIN32

#include "fft_32.h"
#include <fftw3.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include "data.h"

namespace px {
namespace {

struct FftBufferCloser final {
    void operator()(fftw_complex* buffer) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): FFTW allocation ABI.
        fftw_free(buffer);
    }
};

struct FftPlanCloser final {
    void operator()(std::remove_pointer_t<fftw_plan>* plan) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): FFTW plan ABI.
        if (plan != nullptr) {
            fftw_destroy_plan(plan);
        }
    }
};

using UniqueFftBuffer = std::unique_ptr<fftw_complex, FftBufferCloser>;
using UniqueFftPlan = std::unique_ptr<std::remove_pointer_t<fftw_plan>, FftPlanCloser>;

}  // namespace

std::mutex FFT32::fft_mtx_;

void FFT32::DoFFT(std::vector<double>& fft, const std::shared_ptr<Data>& one_channel_pcm_data, int bytes, bool pre_alloc_fft) {
    if (!one_channel_pcm_data || bytes <= 1) {
        return;
    }
    std::lock_guard<std::mutex> guard(fft_mtx_);
    const auto byte_count = (std::min)(one_channel_pcm_data->Size(), static_cast<std::size_t>(bytes));
    const auto sample_count = byte_count / sizeof(std::int16_t);
    if (sample_count == 0) {
        return;
    }

    UniqueFftBuffer input{static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * sample_count))};
    UniqueFftBuffer output{static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * sample_count))};
    if (!input || !output) {
        return;
    }

    const auto data = one_channel_pcm_data->Bytes();
    for (std::size_t index = 0; index < sample_count; ++index) {
        std::int16_t sample{};
        std::memcpy(&sample, data.data() + index * sizeof(sample), sizeof(sample));
        input.get()[index][0] = static_cast<double>(sample) * 5.0 / 32767.0;
        input.get()[index][1] = 0.0;
    }

    UniqueFftPlan plan{fftw_plan_dft_1d(static_cast<int>(sample_count), input.get(), output.get(), FFTW_FORWARD, FFTW_ESTIMATE)};
    if (!plan) {
        return;
    }
    fftw_execute(plan.get());

    const auto target_size = (std::min)(std::size_t{480}, sample_count);
    if (pre_alloc_fft && fft.size() < target_size) {
        fft.resize(target_size);
    }
    for (std::size_t index = 0; index < target_size; ++index) {
        const double real = output.get()[index][0];
        const double imaginary = output.get()[index][1];
        const double magnitude = (std::max)(std::hypot(real, imaginary), std::numeric_limits<double>::min());
        const double value = 36.0 * std::log(magnitude);
        if (pre_alloc_fft) {
            fft[index] = value;
        } else {
            fft.push_back(value);
        }
    }
}

}  // namespace px
#endif
