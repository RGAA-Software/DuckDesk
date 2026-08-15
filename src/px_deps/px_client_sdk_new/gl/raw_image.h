#pragma once
#include <memory>

#ifdef WIN32
#include <d3d11.h>
#include <atlbase.h>
#include <wrl/client.h>
#include <string>
#include <dxgi.h>
#include <DXGI1_2.h>

using namespace Microsoft::WRL;

#endif

extern "C"
{
#include <libavutil/frame.h>
}

namespace tc
{
	enum RawImageFormat {
		kRawImageRGB,
		kRawImageRGBA,
		kRawImageNV12,
		kRawImageI420,
		kRawImageI444,
        kRawImageD3D11Texture,
		kRawImageVulkanAVFrame, //此格式实际是AVFrame(AV_PIX_FMT_VULKAN) 
	};

	class RawImage {
	public:
		static std::shared_ptr<RawImage> Make(char* data, int size, int width, int height, int ch, RawImageFormat format);
		static std::shared_ptr<RawImage> MakeRGB(char* data, int size, int width, int height);
		static std::shared_ptr<RawImage> MakeRGBA(char* data, int size, int width, int height);
		static std::shared_ptr<RawImage> MakeNV12(char* data, int size, int width, int height);
		static std::shared_ptr<RawImage> MakeI420(char* data, int size, int width, int height);
		static std::shared_ptr<RawImage> MakeI444(char* data, int size, int width, int height);
#ifdef WIN32
        static std::shared_ptr<RawImage> MakeD3D11Texture(ComPtr<ID3D11Texture2D> texture, int src_subresource);
#endif
		static std::shared_ptr<RawImage> MakeVulkanAVFrame(AVFrame* av_frame);

		RawImage(char* data, int size, int width, int height, int ch, RawImageFormat format);
		RawImage(AVFrame* av_frame);
		~RawImage();

		char* Data();
		int Size();
		RawImageFormat Format();

		std::shared_ptr<RawImage> Clone();
		void CopyTo(const std::shared_ptr<RawImage>& target);

		// 保存到单个文件
		void SaveYUV444ToFile(const std::string& filename);
		void AppendYUV444ToFile(const std::string& filename);
	public:

		char* img_buf = nullptr;
		int img_buf_size = 0;
		int img_width = 0;
		int img_height = 0;
		int img_ch = -1;
		RawImageFormat img_format;
		AVFrame* vulkan_av_frame_ = nullptr;
		bool full_color_ = false;
#ifdef WIN32
        ComPtr<ID3D11Device> device_ = nullptr;
        ComPtr<ID3D11DeviceContext> device_context_ = nullptr;
        ComPtr<ID3D11Texture2D> texture_ = nullptr;
        int src_subresource_ = 0;
#endif

	};

	typedef std::shared_ptr<RawImage> RawImagePtr;
}