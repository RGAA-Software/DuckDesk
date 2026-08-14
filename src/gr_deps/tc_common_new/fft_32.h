#ifndef FFT_32_H_
#define FFT_32_H_

#ifdef WIN32

#include <mutex>
#include <vector>
#include <functional>

namespace tc
{

    class Data;

    class FFT32 {
    public:

        static void DoFFT(std::vector<double> &fft, const std::shared_ptr<Data>& one_channel_pcm_data, int bytes = 0, bool pre_alloc_fft = false);
        static std::mutex fft_mtx_;

    };

}
#endif

#endif