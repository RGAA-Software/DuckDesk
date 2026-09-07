#ifndef TC_CLIENT_ANDROID_STREAM_HELPER_H
#define TC_CLIENT_ANDROID_STREAM_HELPER_H

#include <string>
#include <string_view>

namespace px {

struct H264ParameterSets final {
    std::string sps{};
    std::string pps{};
};

class StreamHelper final {
  public:
    static H264ParameterSets ExtractH264ParameterSets(std::string_view frame);
    static std::string ExtractH265ParameterSets(std::string_view frame);
};

} // namespace px

#endif // TC_CLIENT_ANDROID_STREAM_HELPER_H
