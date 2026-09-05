#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#if defined(_WIN32)
#if defined(PX_RTC_CLIENT_BUILD)
#define PX_RTC_CLIENT_API __declspec(dllexport)
#else
#define PX_RTC_CLIENT_API __declspec(dllimport)
#endif
#else
#define PX_RTC_CLIENT_API
#endif

namespace px {

class Data;

using OnLocalSdpSetCallback = std::function<void(const std::string&)>;
using OnLocalIceCallback = std::function<void(const std::string& ice, const std::string& mid, int sdp_mline_index)>;
using OnMediaMessageCallback = std::function<void(std::shared_ptr<Data>)>;
using OnFtMessageCallback = std::function<void(std::shared_ptr<Data>)>;
using OnVideoFrameCallback = std::function<void(int width, int height, std::shared_ptr<Data> i420)>;
using OnEncodedVideoFrameCallback = std::function<void(int track_index, bool key_frame, int width, int height, std::shared_ptr<Data> encoded)>;
using OnAudioDataCallback = std::function<void(std::shared_ptr<Data> pcm, int sample_rate, int channels)>;
using OnIceStateCallback = std::function<void(int state)>;
using OnStatsJsonCallback = std::function<void(const std::string& json)>;

// Concrete C++ facade exported by px_client_rtc.dll. The implementation and
// all libwebrtc types stay private to the DLL; consumers link only its import library.
class PX_RTC_CLIENT_API RtcClient final {
  public:
    RtcClient();
    ~RtcClient();

    RtcClient(const RtcClient&) = delete;
    RtcClient& operator=(const RtcClient&) = delete;
    RtcClient(RtcClient&&) = delete;
    RtcClient& operator=(RtcClient&&) = delete;

    [[nodiscard]] bool Init(const std::string& remote_device_id);
    bool Exit();
    bool OnRemoteSdp(const std::string& sdp);
    bool OnRemoteIce(const std::string& ice, const std::string& mid, std::int32_t sdp_mline_index);

    void SetOnLocalSdpSetCallback(OnLocalSdpSetCallback callback);
    void SetOnLocalIceCallback(OnLocalIceCallback callback);
    void SetMediaMessageCallback(OnMediaMessageCallback callback);
    void SetFtMessageCallback(OnFtMessageCallback callback);
    void SetOnVideoFrameCallback(OnVideoFrameCallback callback);
    void SetOnEncodedVideoFrameCallback(OnEncodedVideoFrameCallback callback);
    void SetOnAudioDataCallback(OnAudioDataCallback callback);
    void SetOnIceStateCallback(OnIceStateCallback callback);
    void SetOnStatsJsonCallback(OnStatsJsonCallback callback);

    void PostMediaMessage(std::shared_ptr<Data> message);
    void PostFtMessage(std::shared_ptr<Data> message);
    void PostInputMessage(std::shared_ptr<Data> message);

    [[nodiscard]] std::int64_t GetQueuingMediaMsgCount() const;
    [[nodiscard]] std::int64_t GetQueuingFtMsgCount() const;
    [[nodiscard]] bool HasEnoughBufferForQueuingMediaMessages() const;
    [[nodiscard]] bool HasEnoughBufferForQueuingFtMessages() const;
    [[nodiscard]] bool IsMediaChannelReady() const;
    [[nodiscard]] bool IsFtChannelReady() const;
    [[nodiscard]] bool IsInputChannelReady() const;

    void On16msTimeout();
    void SetLocalRtcMode(bool enabled);
    void SetIceServersJson(const std::string& json);
    [[nodiscard]] bool RestartIce(const std::string& json);
    void SetFileTransferOnly(bool enabled);
    void SetVideoTrackCount(int count);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] PX_RTC_CLIENT_API std::shared_ptr<RtcClient> CreateRtcClient();

} // namespace px
