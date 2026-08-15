//
// Created by RGAA on 2023-12-16.
//

#ifndef TC_ENCODER_TC_ENCODER_H
#define TC_ENCODER_TC_ENCODER_H

#include <cstdint>
#include <functional>
#include <memory>
#ifdef WIN32
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#endif
#include "encoder_config.h"
#include <mutex>

namespace tc
{
	using namespace Microsoft::WRL;

    class Data;
	class Image;
    class Thread;
    class MessageNotifier;
    class MessageListener;
    class FrameRender;

	enum VideoEncoderFormat {
	    kH264,
	    kHEVC,
	};

	struct VideoEncoderParams {
	    int width_;
	    int height_;
	    VideoEncoderFormat format_;
	};

	using EncoderCallback = std::function<void(const std::shared_ptr<Image>& frame, uint64_t frame_index, bool key)>;

	class VideoEncoder {
	public:
	    VideoEncoder(const std::shared_ptr<MessageNotifier>& msg_notifier, const EncoderFeature& encoder_feature);
	    virtual ~VideoEncoder();

	    virtual bool Initialize(const tc::EncoderConfig& config);

	    void RegisterEncodeCallback(EncoderCallback&& cbk);
	    void InsertIDR();

	    virtual void Encode(ID3D11Texture2D* tex2d, uint64_t frame_index);
	    virtual void Encode(const std::shared_ptr<Image>& i420_data, uint64_t frame_index);
	    virtual void Exit();

//        bool D3D11Texture2DLockMutex(ComPtr<ID3D11Texture2D> texture2d);
//        bool D3D11Texture2DReleaseMutex(ComPtr<ID3D11Texture2D> texture2d);
//        bool CopyID3D11Texture2D(ComPtr<ID3D11Texture2D> shared_texture2d);
//        ComPtr<ID3D11Texture2D> OpenSharedTexture(HANDLE handle);

    private:
        void ListenMessages();

	protected:
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        int refresh_rate_ = 0;
	    int input_frame_width_ = 0;
	    int input_frame_height_ = 0;
	    int out_height_ = 0;
	    int out_width_ = 0;

	    uint64_t encoded_frame_index_ = 0;
        EncoderConfig encoder_config_;
        EncoderFeature encoder_feature_;
	    EncoderCallback encoder_callback_;
	    bool insert_idr_ = false;

	    ComPtr<ID3D11Device> d3d11_device_;
	    ComPtr<ID3D11DeviceContext> d3d11_device_context_;
	    ComPtr<ID3D11Texture2D> texture2d_;
        std::shared_ptr<FrameRender> frame_render_ = nullptr;

	};

}

#endif //TC_ENCODER_TC_ENCODER_H
