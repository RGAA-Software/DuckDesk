//
// Created by RGAA on 29/08/2025.
//

#include <QSurface>
#include "ct_d3d11_video_widget.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/thread.h"
#include "px_common_new/time_util.h"
#include "px_common_new/file.h"
#include "px_common_new/win32/d3d11_wrapper.h"
#include "px_client_sdk_new/sdk_params.h"
#include "px_client_sdk_new/thunder_sdk.h"
#include "d3d11_render_manager.h"
#include "raw_sdl_widget.h"
#include <atomic>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>

namespace px
{

    namespace {
        // [LAT-render] 客户端单帧渲染耗时统计(纹理上传 + Draw + Present)
        std::atomic<uint64_t> g_render_frames{0};
        std::atomic<uint64_t> g_render_us_sum{0};
        std::atomic<uint64_t> g_render_us_max{0};

        void DumpRenderLatencyIfDue() {
            static std::atomic<uint64_t> s_last_dump_us{0};
            auto now = TimeUtil::GetCurrentTimePointUS();
            auto last = s_last_dump_us.load();
            if (now - last < 5000000) {
                return;
            }
            if (!s_last_dump_us.compare_exchange_weak(last, now)) {
                return;
            }
            auto n = g_render_frames.exchange(0);
            auto sum = g_render_us_sum.exchange(0);
            auto mx = g_render_us_max.exchange(0);
            LOGI("[LAT-render] frames={} avg_us={} max_us={}",
                 n, n > 0 ? (sum / n) : 0, mx);
        }

        bool UploadCpuPlane(
            const ComPtr<ID3D11DeviceContext>& context,
            const ComPtr<ID3D11Texture2D>& texture,
            const std::shared_ptr<RawImage>& image,
            std::size_t source_offset,
            int source_stride,
            int row_bytes,
            int row_count,
            std::string_view plane_name) {
            if (!context || !texture || !image || !image->Data()
                || source_offset + static_cast<std::size_t>(source_stride) * row_count
                    > static_cast<std::size_t>(image->Size())) {
                LOGE("Invalid CPU {} plane: offset={}, stride={}, rows={}, image_bytes={}",
                     plane_name, source_offset, source_stride, row_count,
                     image ? image->Size() : 0);
                return false;
            }

            D3D11_MAPPED_SUBRESOURCE resource{};
            const auto result = context->Map(
                texture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
            if (FAILED(result) || !resource.pData || resource.RowPitch < row_bytes) {
                LOGE("Failed to map CPU {} plane: hr={}, row_pitch={}, row_bytes={}",
                     plane_name, static_cast<long>(result), resource.RowPitch, row_bytes);
                if (SUCCEEDED(result) && resource.pData) {
                    context->Unmap(texture.Get(), 0);
                }
                return false;
            }

            const auto mapped = std::span(
                static_cast<std::byte*>(resource.pData),
                static_cast<std::size_t>(resource.RowPitch) * row_count);
            for (int row = 0; row < row_count; ++row) {
                std::memcpy(
                    mapped.data() + static_cast<std::size_t>(resource.RowPitch) * row,
                    image->Data() + source_offset + static_cast<std::size_t>(source_stride) * row,
                    row_bytes);
            }
            context->Unmap(texture.Get(), 0);
            return true;
        }
    }


    D3D11VideoWidget::D3D11VideoWidget(const std::shared_ptr<ClientContext> &ctx, const std::shared_ptr<ThunderSdk> &sdk,
                                   int dup_idx, RawImageFormat format, QWidget *parent)
            : QWidget(parent), VideoWidget(ctx, sdk, dup_idx) {
        this->raw_image_format_ = format;
        
        QPalette pal = palette();
        pal.setColor(QPalette::Window, Qt::black);
        setAutoFillBackground(true);
        setPalette(pal);

        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_NativeWindow);

        // Setting these attributes to our widget and returning null on paintEngine event
        // tells Qt that we'll handle all drawing and updating the widget ourselves.
        setAttribute(Qt::WA_PaintOnScreen);
        setAttribute(Qt::WA_NoSystemBackground);

        setMouseTracking(true);
        //grabKeyboard();

        render_mgr_ = std::make_shared<D3D11RenderManager>();

        ///
        // raw_sdl_widget_ = new RawSdlWidget();

        //nv12_frame = ReadNV12FromFile();

    }

    D3D11VideoWidget::~D3D11VideoWidget() {

    }

    bool D3D11VideoWidget::InitD3DEnvIfNeeded(RawImageFormat raw_format, int fw, int fh, ComPtr<ID3D11Device> device,  ComPtr<ID3D11DeviceContext> device_context) {
        if (init) {
            return true;
        }

        // Ignore to initialize when the Window was hidden or too small
        if (this->isHidden() || this->size().width() <= 256) {
            LOGW("D3D11VideoWidget is not valid, hidden?: {}, size: {}x{}", this->isHidden(), this->width(), this->height());
            return false;
        }

        renderer_ready_ = false;
        if (auto r = render_mgr_->InitOutput((HWND)winId(), raw_format, fw, fh, device, device_context);
            r == DUPL_RETURN_SUCCESS) {
            this->init = true;
            this->tex_width_ = fw;
            this->tex_height_ = fh;
            this->raw_image_format_ = raw_format;
            LOGI("D3D11 render init success by size: {}x{}", fw, fh);
        }
        else {
            LOGI("D3D output init failed: {}", (int)r);
            return false;
        }
        renderer_ready_ = true;
        return true;
    }

    QPaintEngine * D3D11VideoWidget::paintEngine() const
    {
        return Q_NULLPTR;
    }

    void D3D11VideoWidget::paintEvent(QPaintEvent * event) {

    }

    void D3D11VideoWidget::RefreshImage(const std::shared_ptr<RawImage>& image) {
        auto beg = TimeUtil::GetCurrentTimestamp();
        auto beg_us = TimeUtil::GetCurrentTimePointUS();

        if (!image) {
            LOGE("Cannot render an empty image");
            return;
        }

        const bool is_cpu_yuv = image->Format() == RawImageFormat::kRawImageI420
            || image->Format() == RawImageFormat::kRawImageI444;
        auto render_device = image->device_;
        auto render_context = image->device_context_;
        if ((!render_device || !render_context)
            && is_cpu_yuv
            && sdk_) {
            const auto params = sdk_->GetSdkParams();
            const auto wrapper = params ? params->d3d11_wrapper_ : nullptr;
            if (wrapper && wrapper->IsValid()) {
                render_device = wrapper->d3d11_device_;
                render_context = wrapper->d3d11_device_context_;
            }
        }

        if (!render_device || !render_context) {
            LOGE("Cannot render frame format {} because no D3D11 device/context is available",
                 static_cast<int>(image->Format()));
            return;
        }

        if (!InitD3DEnvIfNeeded(image->Format(), image->img_width, image->img_height,
                                render_device, render_context)) {
            LOGE("Don't have d3d environment.");
            return;
        }

        // LOGI("this image format: {}, image in: {}", this->raw_image_format_, image->img_format);
        if (this->tex_width_ != image->img_width || this->tex_height_ != image->img_height || this->raw_image_format_ != image->img_format) {
            renderer_ready_ = false;
            if (render_mgr_->RecreateTexture(image->img_format, image->img_width, image->img_height) == DUPL_RETURN_SUCCESS) {
                this->tex_width_ = image->img_width;
                this->tex_height_ = image->img_height;
                this->raw_image_format_ = image->img_format;
                renderer_ready_ = true;
            }
            else {
                LOGE("Recreate Texture failed!");
                return;
            }
        }

        if (image->Format() == RawImageFormat::kRawImageD3D11Texture) {
            auto texture = render_mgr_->GetTexture();
            if (!texture) {
                LOGE("Don't have a valid texture to draw.");
                return;
            }
            if (!image->texture_) {
                LOGE("The image format is D3D11Texture, but there isn't a valid texture.");
                return;
            }
            this->RefreshD3DImage(image);
        }
        else {
            // Flush to GPU memory directly
            if (is_cpu_yuv) {
                const int chroma_width = image->Format() == RawImageFormat::kRawImageI420
                    ? image->img_width / 2 : image->img_width;
                const int chroma_height = image->Format() == RawImageFormat::kRawImageI420
                    ? image->img_height / 2 : image->img_height;
                const std::size_t y_bytes = static_cast<std::size_t>(image->img_width)
                    * image->img_height;
                const std::size_t chroma_bytes = static_cast<std::size_t>(chroma_width)
                    * chroma_height;
                if (!UploadCpuPlane(render_context, render_mgr_->GetYPlane(), image,
                                    0, image->img_width, image->img_width,
                                    image->img_height, "Y")
                    || !UploadCpuPlane(render_context, render_mgr_->GetUPlane(), image,
                                       y_bytes, chroma_width, chroma_width,
                                       chroma_height, "U")
                    || !UploadCpuPlane(render_context, render_mgr_->GetVPlane(), image,
                                       y_bytes + chroma_bytes, chroma_width, chroma_width,
                                       chroma_height, "V")) {
                    return;
                }
            }

            bool Occluded = false;
            auto Ret = render_mgr_->UpdateApplicationWindow(&Occluded);
        }

        auto end = TimeUtil::GetCurrentTimestamp();
        //LOGI("Refresh image used: {}ms", (end - beg));
        // [LAT-render] 计时单帧渲染(纹理上传 + Draw + Present)耗时
        {
            auto us = TimeUtil::GetCurrentTimePointUS() - beg_us;
            ++g_render_frames;
            g_render_us_sum += us;
            auto prev = g_render_us_max.load();
            while (us > prev && !g_render_us_max.compare_exchange_weak(prev, us)) {}
            DumpRenderLatencyIfDue();
        }
        fps_stat_.Tick();

        // For testing
        // raw_sdl_widget_->RefreshImage(image);
    }

    void D3D11VideoWidget::RefreshD3DImage(const std::shared_ptr<RawImage>& image) {
        if (!renderer_ready_) {
            LOGW("Renderer not ready !!!");
            return;
        }
        if (render_mgr_->GetFrameWidth() != image->img_width || render_mgr_->GetFrameHeight() != image->img_height) {
            LOGW("Frame size not equals, render mgr: {}x{} ==> new image: {}x{}", render_mgr_->GetFrameWidth(), render_mgr_->GetFrameHeight(),
                 image->img_width, image->img_height);
            return;
        }
        D3D11_BOX srcBox;
        srcBox.left = 0;
        srcBox.top = 0;
        srcBox.right = image->img_width;
        srcBox.bottom = image->img_height;
        srcBox.front = 0;
        srcBox.back = 1;
        image->device_context_->CopySubresourceRegion(render_mgr_->GetTexture().Get(), 0, 0, 0, 0, image->texture_.Get(), image->src_subresource_, &srcBox);

        bool occluded = false;
        auto r = render_mgr_->UpdateApplicationWindow(&occluded);
        if (r != DUPL_RETURN_SUCCESS) {
            LOGE("Draw Texture failed, maybe retry to initialize the d3d11.");
        }
    }

    void D3D11VideoWidget::resizeEvent(QResizeEvent* event) {
        QWidget::resizeEvent(event);
        render_mgr_->WindowResize();
    }

    void D3D11VideoWidget::mouseMoveEvent(QMouseEvent* e) {
        QWidget::mouseMoveEvent(e);
        VideoWidget::OnMouseMoveEvent(e, QWidget::width(), QWidget::height());
    }

    void D3D11VideoWidget::mousePressEvent(QMouseEvent* e) {
        QWidget::mousePressEvent(e);
        VideoWidget::OnMousePressEvent(e, QWidget::width(), QWidget::height());
    }

    void D3D11VideoWidget::mouseReleaseEvent(QMouseEvent* e) {
        QWidget::mouseReleaseEvent(e);
        VideoWidget::OnMouseReleaseEvent(e, QWidget::width(), QWidget::height());
    }

    void D3D11VideoWidget::mouseDoubleClickEvent(QMouseEvent* e) {
        QWidget::mouseDoubleClickEvent(e);
        VideoWidget::OnMouseDoubleClickEvent(e);
    }

    void D3D11VideoWidget::wheelEvent(QWheelEvent* e) {
        QWidget::wheelEvent(e);
        VideoWidget::OnWheelEvent(e, QWidget::width(), QWidget::height());
    }

    void D3D11VideoWidget::keyPressEvent(QKeyEvent* e) {
        QWidget::keyPressEvent(e);
        VideoWidget::OnKeyPressEvent(e);
    }

    void D3D11VideoWidget::keyReleaseEvent(QKeyEvent* e) {
        QWidget::keyReleaseEvent(e);
        VideoWidget::OnKeyReleaseEvent(e);
    }

    void D3D11VideoWidget::closeEvent(QCloseEvent* event) {
        //QWidget::closeEvent(event);
    }

    void D3D11VideoWidget::focusInEvent(QFocusEvent *event) {
        grabKeyboard();
    }

    void D3D11VideoWidget::focusOutEvent(QFocusEvent *event) {
        releaseKeyboard();
    }

    QWidget* D3D11VideoWidget::AsWidget() {
        return dynamic_cast<QWidget*>(this);
    }

    void D3D11VideoWidget::OnTimer1S() {
        // LOGI("D3D11 refresh FPS: {}", fps_stat_.value());
    }

    WId D3D11VideoWidget::GetRenderWId() {
        return this->winId();
    }

    QImage D3D11VideoWidget::CaptureImage() {
        return render_mgr_->SaveBackBufferToImage();
    }

}
