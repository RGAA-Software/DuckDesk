//
// Created by RGAA on 19/11/2024.
//

#ifndef PX_VIDEO_FRAME_CARRIER_H
#define PX_VIDEO_FRAME_CARRIER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
#ifdef WIN32
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#endif
#include <mutex>
//#include <opencv2/opencv.hpp>
#include "px_common/image.h"

namespace px
{
    using namespace Microsoft::WRL;

    class Image;
    class Thread;

    struct VideoFrameCarrierResources final {
        std::shared_ptr<Image> logo_image;
        std::vector<std::pair<int, int>> logo_points;
        std::vector<std::pair<int, int>> big_logo_points;
        std::vector<std::pair<int, int>> cover_points;
    };

    // move video frames from provider / capture
    class VideoFrameCarrier final
        : public std::enable_shared_from_this<VideoFrameCarrier> {
    public:
        [[nodiscard]] static std::shared_ptr<VideoFrameCarrier> Create(
            VideoFrameCarrierResources resources,
            const ComPtr<ID3D11Device>& d3d11_device,
            const ComPtr<ID3D11DeviceContext>& d3d11_device_context,
            uint64_t adapter_uid,
            const std::string& monitor_name,
            bool enable_full_color_mode);
        explicit VideoFrameCarrier(
            VideoFrameCarrierResources resources,
            const ComPtr<ID3D11Device>& d3d11_device,
            const ComPtr<ID3D11DeviceContext>& d3d11_device_context,
            uint64_t adapter_uid,
            const std::string& monitor_name,
            bool enable_full_color_mode);

        bool MapRawTexture(const ComPtr<ID3D11Texture2D>& texture, DXGI_FORMAT format, int height,
                           std::function<void(const std::shared_ptr<Image>&)>&& rgba_cbk,
                           std::function<void(const std::shared_ptr<Image>&)>&& yuv_cbk);

        ComPtr<ID3D11Texture2D> CopyTexture(const std::string& mon_name, uint64_t handle, uint64_t frame_index);

        bool ConvertRawImage(const std::shared_ptr<Image> image,
                            std::function<void(const std::shared_ptr<Image>&)>&& rgba_cbk,
                            std::function<void(const std::shared_ptr<Image>&)>&& yuv_cbk);

        void Exit();
        void SetFullColorModeEnabled(bool enabled);
        void ChangeLogoPosition();

    private:
        static bool D3D11Texture2DLockMutex(const ComPtr<ID3D11Texture2D>& texture2d);
        static bool D3D11Texture2DReleaseMutex(const ComPtr<ID3D11Texture2D>& texture2d);
        bool CopyID3D11Texture2D(const ComPtr<ID3D11Texture2D>& shared_texture2d);
        // R10G10B10A2 等非 8bit 格式 NVENC(H264) 无法编码,先 shader blit 成 B8G8R8A8。
        static bool IsEncoderFriendlyFormat(DXGI_FORMAT format);
        bool EnsureConvertShaders();
        bool BlitConvertToBgra(const ComPtr<ID3D11Texture2D>& src);
        ComPtr<ID3D11Texture2D> OpenSharedTexture(HANDLE handle);
        // 缓存按 handle 打开的共享纹理:对 11on12(D3D12 游戏)共享资源每帧
        // open/close 会导致 GPU device removed (TDR),必须与 OBS 一样长期持有。
        ComPtr<ID3D11Texture2D> opened_shared_texture_ = nullptr;
        uint64_t opened_shared_handle_ = 0;
        bool CopyToRawImage(
            std::span<const std::byte> data,
            int row_pitch_bytes,
            int height);
        void ConvertToYuv420(std::function<void(const std::shared_ptr<Image>&)>&& yuv_cbk);
        void ConvertToYuv444(std::function<void(const std::shared_ptr<Image>&)>&& yuv_cbk);
        [[nodiscard]] int GetRawImageType() const;
        void StampLogoOnTexture(const ComPtr<ID3D11Texture2D>& texture, int tex_width, int tex_height);
        void StampLogoOnRGBABuffer(const std::shared_ptr<Image>& image);

    private:
        ComPtr<ID3D11Device> d3d11_device_ = nullptr;
        ComPtr<ID3D11DeviceContext> d3d11_device_context_ = nullptr;
        ComPtr<ID3D11Texture2D> texture2d_ = nullptr;
        // staging copy used only by MapRawTexture when the source texture is not CPU-readable
        // (CopyID3D11Texture2D now keeps a DEFAULT-usage texture for GPU encoders such as NVENC,
        //  so CPU encoders like ffmpeg/QSV need this readback copy).
        ComPtr<ID3D11Texture2D> map_staging_texture_ = nullptr;

        std::shared_ptr<Image> raw_image_rgba_ = nullptr;
        int raw_image_rgba_format_ = -1;
        std::shared_ptr<Image> raw_image_yuv_ = nullptr;

        std::shared_ptr<Image> logo_image_;

        // async yuv converter
        std::shared_ptr<Thread> yuv_converter_thread_ = nullptr;

        uint64_t adapter_uid_ = 0;
        std::string monitor_name_;

        bool enable_full_color_mode_ = false;

        // logo points
        std::vector<std::pair<int, int>> logo_points_;
        // big log points
        std::vector<std::pair<int, int>> big_logo_points_;
        // cover points
        std::vector<std::pair<int, int>> cover_points_;

        ComPtr<ID3D11Texture2D> logo_point_texture_ = nullptr;

        // 10bit→8bit 转换用的全屏三角形 shader(懒加载,d3dcompiler_47 系统自带)
        ComPtr<ID3D11VertexShader> conv_vs_ = nullptr;
        ComPtr<ID3D11PixelShader> conv_ps_ = nullptr;
        bool conv_shader_failed_ = false;

        bool logo_pos_offset_ = true;

        bool enable_logo_ = true;
    };

}

#endif //PX_VIDEO_FRAME_CARRIER_H
