//
// Created by hy on 2024/1/24.
//

#ifndef TC_CLIENT_ANDROID_FRAME_RENDER_H
#define TC_CLIENT_ANDROID_FRAME_RENDER_H

#include <atomic>
#include <memory>
#include <mutex>

#include <android/native_window_jni.h>
#include <jni.h>

#include "px_client_sdk/gl/raw_image.h"
#include "px_client_sdk/sdk_decoder_render_type.h"
#include "gl/shader_program.h"

namespace px
{

    class AppContext;
    class MessageListener;

    struct NativeWindowReleaser {
        void operator()(ANativeWindow* window) const noexcept; // NOLINT(gammaray-raw-pointer-boundary): Android NDK release ABI
    };

    class FrameRender : public std::enable_shared_from_this<FrameRender> {
    public:

        static std::shared_ptr<FrameRender> Make(const std::shared_ptr<AppContext>& ctx);

        explicit FrameRender(const std::shared_ptr<AppContext>& ctx);
        ~FrameRender();

        void Init(JNIEnv* env, jobject surface, const DecoderRenderType& drt, int oes_tex_id);
        void UpdateYUVImage(const std::shared_ptr<RawImage>& image);
        void TickRefresh(JNIEnv* env);
        ANativeWindow* GetNativeWindow();

        void OnCreate();
        void OnResume();
        void OnPause();
        void OnDestroy();

    private:

        void RegisterListeners();

    private:

        std::shared_ptr<AppContext> app_context_ = nullptr;
        std::shared_ptr<MessageListener> bus_listener_ = nullptr;

        DecoderRenderType decoder_render_type_;

        GLuint program_;

        // another texture for decoder
        GLuint video_vao_ = 0;
        GLuint oes_tex_id_ = 0;
        std::unique_ptr<ANativeWindow, NativeWindowReleaser> decode_win_surface_;
        bool use_oes_ = false;

        // I420
        GLuint img_textures_[3] = {0};

        // NV12
        GLuint y_texture_id_ = 0;
        GLuint uv_texture_id_ = 0;

        std::mutex raw_image_mtx_;
        std::shared_ptr<RawImage> current_raw_image_ = nullptr;

        std::atomic_bool need_init_texture_{false};
        bool is_gl_inited_ = false;
        std::atomic_bool exit_{false};
    };

}

#endif //TC_CLIENT_ANDROID_FRAME_RENDER_H
