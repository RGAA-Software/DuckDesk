#include "vulkan_renderer.h"

#include "window_host.h"

#include "px_client_sdk/gl/raw_image.h"
#include "px_client_sdk/platform/windows/windows_video_frame.h"
#include "px_common/log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <libplacebo/renderer.h>
#include <libplacebo/vulkan.h>
#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace px::desktop {
namespace {

constexpr std::array kOptionalDeviceExtensions{VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,   VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
                                               VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,       VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME,
                                               VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME, VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME};

void PlaceboLog(void*, const enum pl_log_level level,
                const char* message) { // NOLINT(pixels-raw-pointer-boundary): synchronous libplacebo logging callback ABI
    if (!message)
        return;
    if (level <= PL_LOG_ERR)
        LOGE("[libplacebo] {}", message);
    else if (level <= PL_LOG_WARN)
        LOGW("[libplacebo] {}", message);
}

bool VulkanSucceeded(const VkResult result, const std::string_view operation) {
    if (result == VK_SUCCESS)
        return true;
    LOGE("Vulkan operation {} failed with {}", operation, static_cast<int>(result));
    return false;
}

bool HasEnabledExtension(const pl_vulkan vulkan, const std::string_view required) {
    if (!vulkan)
        return false;
    for (int index{}; index < vulkan->num_extensions; ++index) {
        if (vulkan->extensions[index] && required == vulkan->extensions[index])
            return true;
    }
    return false;
}

bool FfmpegSupportsVulkan(const AVCodecID codecId) {
    const AVCodec* decoder = avcodec_find_decoder(codecId); // NOLINT(pixels-raw-pointer-boundary): borrowed FFmpeg codec descriptor
    if (!decoder)
        return false;
    for (int index{};; ++index) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, index); // NOLINT(pixels-raw-pointer-boundary): borrowed FFmpeg descriptor
        if (!config)
            return false;
        if (config->device_type == AV_HWDEVICE_TYPE_VULKAN && config->pix_fmt == AV_PIX_FMT_VULKAN)
            return true;
    }
}

bool SupportsNativeVulkanVideo(const pl_vulkan vulkan) {
    if (!vulkan)
        return false;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(vulkan->phys_device, &properties);
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
        LOGW("Vulkan software device [{}] is not eligible for native video", properties.deviceName);
        return false;
    }
    const bool h264{HasEnabledExtension(vulkan, VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME) && FfmpegSupportsVulkan(AV_CODEC_ID_H264)};
    const bool hevc{HasEnabledExtension(vulkan, VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME) && FfmpegSupportsVulkan(AV_CODEC_ID_HEVC)};
    LOGI("Vulkan native video candidate [{}], H264={}, HEVC={}", properties.deviceName, h264, hevc);
    return h264 && hevc;
}

std::shared_ptr<RawImage> DownloadD3dNv12Frame(const D3D11Image& source) {
    if (!source.source_frame)
        return {};
    auto downloaded = AllocateAvFrame();
    if (!downloaded || av_hwframe_transfer_data(downloaded.get(), source.source_frame.get(), 0) < 0 || downloaded->format != AV_PIX_FMT_NV12) {
        return {};
    }
    auto image = RawImage::Make(kRawImageNV12, downloaded->width, downloaded->height);
    if (!image)
        return {};
    for (std::size_t index{}; index < 2; ++index) {
        const auto layout = image->Layout(index);
        if (!layout || !downloaded->data[index] || downloaded->linesize[index] == 0 || std::abs(downloaded->linesize[index]) < layout->stride) {
            return {};
        }
        auto destination = image->MutablePlane(index);
        for (int row{}; row < layout->rows; ++row) {
            const std::span<const char> sourceRow{reinterpret_cast<const char*>(downloaded->data[index]) +
                                                      static_cast<std::ptrdiff_t>(row) * downloaded->linesize[index],
                                                  static_cast<std::size_t>(layout->stride)};
            std::ranges::copy(sourceRow, destination.subspan(static_cast<std::size_t>(row) * layout->stride, layout->stride).begin());
        }
    }
    return image;
}

} // namespace

struct VulkanRenderer::Impl final {
    explicit Impl(const WindowHost& windowValue) : window{std::cref(windowValue)} {}

    std::reference_wrapper<const WindowHost> window;
    pl_log log{};
    pl_vk_inst instance{};
    pl_vulkan vulkan{};
    pl_renderer videoRenderer{};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    std::uint32_t queueFamily{};
    ImGui_ImplVulkanH_Window swapchain{};
    AvBufferPtr decoderDevice{};
    std::array<pl_tex, PL_MAX_PLANES> mappedTextures{};
    std::array<pl_tex, 3> uploadTextures{};
    pl_tex videoTarget{};
    VkImageView videoTargetView{VK_NULL_HANDLE};
    VkDescriptorSet videoDescriptor{VK_NULL_HANDLE};
    VkSemaphore videoReady{VK_NULL_HANDLE};
    VkFence lastSubmittedFence{VK_NULL_HANDLE};
    int videoWidth{};
    int videoHeight{};
    int pendingWidth{};
    int pendingHeight{};
    bool resizePending{};
    bool imguiBackendInitialized{};
    bool swapchainCreated{};
    bool videoTargetHeld{};
    bool videoReadyPending{};

    ~Impl() {
        if (vulkan)
            static_cast<void>(vkDeviceWaitIdle(vulkan->device));
        ReleaseVideoTarget();
        for (auto& texture : mappedTextures) {
            if (texture && vulkan)
                pl_tex_destroy(vulkan->gpu, &texture);
        }
        for (auto& texture : uploadTextures) {
            if (texture && vulkan)
                pl_tex_destroy(vulkan->gpu, &texture);
        }
        if (videoReady != VK_NULL_HANDLE && vulkan)
            vkDestroySemaphore(vulkan->device, videoReady, nullptr);
        decoderDevice.reset();
        if (swapchainCreated && vulkan) {
            ImGui_ImplVulkanH_DestroyWindow(instance->instance, vulkan->device, &swapchain, nullptr);
            swapchainCreated = false;
        }
        if (surface != VK_NULL_HANDLE && instance) {
            vkDestroySurfaceKHR(instance->instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        if (videoRenderer)
            pl_renderer_destroy(&videoRenderer);
        if (vulkan)
            pl_vulkan_destroy(&vulkan);
        if (instance)
            pl_vk_inst_destroy(&instance);
        if (log)
            pl_log_destroy(&log);
    }

    static void LockDecoderQueue(AVHWDeviceContext* context, // NOLINT(pixels-raw-pointer-boundary): retained FFmpeg callback ABI
                                 const std::uint32_t family, const std::uint32_t index) {
        const auto self = static_cast<Impl*>(context->user_opaque); // NOLINT: FFmpeg retains callback context for device lifetime
        if (self && self->vulkan)
            self->vulkan->lock_queue(self->vulkan, family, index);
    }

    static void UnlockDecoderQueue(AVHWDeviceContext* context, // NOLINT(pixels-raw-pointer-boundary): retained FFmpeg callback ABI
                                   const std::uint32_t family, const std::uint32_t index) {
        const auto self = static_cast<Impl*>(context->user_opaque); // NOLINT: FFmpeg retains callback context for device lifetime
        if (self && self->vulkan)
            self->vulkan->unlock_queue(self->vulkan, family, index);
    }

    bool CreateDecoderDevice() {
        decoderDevice = AvBufferPtr{av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN), AvBufferDeleter{}};
        if (!decoderDevice)
            return false;
        auto& hardware = *reinterpret_cast<AVHWDeviceContext*>(decoderDevice->data); // NOLINT: FFmpeg context boundary
        auto& context = *reinterpret_cast<AVVulkanDeviceContext*>(hardware.hwctx);   // NOLINT: FFmpeg context boundary
        hardware.user_opaque = this; // NOLINT: required FFmpeg queue callback context; renderer owns the AVBuffer lifetime
        context.get_proc_addr = instance->get_proc_addr;
        context.inst = instance->instance;
        context.phys_dev = vulkan->phys_device;
        context.act_dev = vulkan->device;
        context.device_features = *vulkan->features;
        context.enabled_inst_extensions = instance->extensions;
        context.nb_enabled_inst_extensions = instance->num_extensions;
        context.enabled_dev_extensions = vulkan->extensions;
        context.nb_enabled_dev_extensions = vulkan->num_extensions;
        context.lock_queue = LockDecoderQueue;
        context.unlock_queue = UnlockDecoderQueue;

        std::uint32_t familyCount{};
        vkGetPhysicalDeviceQueueFamilyProperties2(vulkan->phys_device, &familyCount, nullptr);
        familyCount = std::min<std::uint32_t>(familyCount, static_cast<std::uint32_t>(std::size(context.qf)));
        std::vector<VkQueueFamilyProperties2> families(familyCount);
        std::vector<VkQueueFamilyVideoPropertiesKHR> videoFamilies(familyCount);
        for (std::uint32_t index{}; index < familyCount; ++index) {
            videoFamilies[index].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
            families[index].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
            families[index].pNext = &videoFamilies[index];
        }
        vkGetPhysicalDeviceQueueFamilyProperties2(vulkan->phys_device, &familyCount, families.data());
        for (std::uint32_t index{}; index < familyCount; ++index) {
            context.qf[index].idx = static_cast<int>(index);
            context.qf[index].num = static_cast<int>(families[index].queueFamilyProperties.queueCount);
            context.qf[index].flags = static_cast<VkQueueFlagBits>(families[index].queueFamilyProperties.queueFlags);
            context.qf[index].video_caps = static_cast<VkVideoCodecOperationFlagBitsKHR>(videoFamilies[index].videoCodecOperations);
        }
        context.nb_qf = static_cast<int>(familyCount);
        if (av_hwdevice_ctx_init(decoderDevice.get()) < 0) {
            decoderDevice.reset();
            return false;
        }
        return true;
    }

    bool CreateSwapchain() {
        VkBool32 presentSupported{};
        if (vkGetPhysicalDeviceSurfaceSupportKHR(vulkan->phys_device, queueFamily, surface, &presentSupported) != VK_SUCCESS || !presentSupported)
            return false;
        constexpr std::array formats{VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
        swapchain.Surface = surface;
        swapchain.SurfaceFormat =
            ImGui_ImplVulkanH_SelectSurfaceFormat(vulkan->phys_device, surface, formats.data(), formats.size(), VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
        constexpr std::array presentModes{VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR};
        swapchain.PresentMode =
            ImGui_ImplVulkanH_SelectPresentMode(vulkan->phys_device, surface, presentModes.data(), static_cast<int>(presentModes.size()));
        int width{};
        int height{};
        SDL_GetWindowSizeInPixels(&window.get().Native(), &width, &height);
        const std::uint32_t minimumImages{static_cast<std::uint32_t>(ImGui_ImplVulkanH_GetMinImageCountFromPresentMode(swapchain.PresentMode))};
        ImGui_ImplVulkanH_CreateOrResizeWindow(instance->instance, vulkan->phys_device, vulkan->device, &swapchain, queueFamily, nullptr, width,
                                               height, std::max(2U, minimumImages), 0);
        swapchainCreated = swapchain.Swapchain != VK_NULL_HANDLE;
        return swapchainCreated;
    }

    void WaitForLastSubmission() {
        if (lastSubmittedFence != VK_NULL_HANDLE && vulkan) {
            static_cast<void>(vkWaitForFences(vulkan->device, 1, &lastSubmittedFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max()));
            lastSubmittedFence = VK_NULL_HANDLE;
        }
    }

    void ReleaseVideoTarget() {
        if (!vulkan)
            return;
        WaitForLastSubmission();
        if (videoDescriptor != VK_NULL_HANDLE && imguiBackendInitialized) {
            ImGui_ImplVulkan_RemoveTexture(videoDescriptor);
            videoDescriptor = VK_NULL_HANDLE;
        }
        if (videoTargetHeld) {
            const pl_vulkan_release_params release{.tex = videoTarget, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .qf = queueFamily};
            pl_vulkan_release_ex(vulkan->gpu, &release);
            videoTargetHeld = false;
        }
        if (videoTargetView != VK_NULL_HANDLE) {
            vkDestroyImageView(vulkan->device, videoTargetView, nullptr);
            videoTargetView = VK_NULL_HANDLE;
        }
        if (videoTarget)
            pl_tex_destroy(vulkan->gpu, &videoTarget);
        videoWidth = 0;
        videoHeight = 0;
        videoReadyPending = false;
    }

    bool EnsureVideoTarget(const int width, const int height) {
        if (videoTarget && videoWidth == width && videoHeight == height)
            return true;
        ReleaseVideoTarget();
        const pl_fmt format = pl_find_named_fmt(vulkan->gpu, "rgba8");
        if (!format)
            return false;
        const pl_tex_params parameters{.w = width, .h = height, .format = format, .sampleable = true, .renderable = true};
        if (!pl_tex_recreate(vulkan->gpu, &videoTarget, &parameters))
            return false;
        VkFormat imageFormat{};
        const VkImage image{pl_vulkan_unwrap(vulkan->gpu, videoTarget, &imageFormat, nullptr)};
        if (image == VK_NULL_HANDLE)
            return false;
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = imageFormat;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        if (!VulkanSucceeded(vkCreateImageView(vulkan->device, &view, nullptr, &videoTargetView), "create video image view"))
            return false;
        videoDescriptor = ImGui_ImplVulkan_AddTexture(videoTargetView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        videoWidth = width;
        videoHeight = height;
        return videoDescriptor != VK_NULL_HANDLE;
    }

    pl_frame TargetFrame() const {
        pl_frame target{};
        target.num_planes = 1;
        target.planes[0].texture = videoTarget;
        target.planes[0].components = 4;
        target.planes[0].component_mapping[0] = 0;
        target.planes[0].component_mapping[1] = 1;
        target.planes[0].component_mapping[2] = 2;
        target.planes[0].component_mapping[3] = 3;
        target.crop = {0.0F, 0.0F, static_cast<float>(videoWidth), static_cast<float>(videoHeight)};
        target.repr = pl_color_repr_rgb;
        target.color = pl_color_space_srgb;
        return target;
    }

    bool RenderSource(const pl_frame& source) {
        WaitForLastSubmission();
        if (videoTargetHeld) {
            const pl_vulkan_release_params release{.tex = videoTarget, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .qf = queueFamily};
            pl_vulkan_release_ex(vulkan->gpu, &release);
            videoTargetHeld = false;
        }
        auto target = TargetFrame();
        if (!pl_render_image(videoRenderer, &source, &target, &pl_render_fast_params))
            return false;
        const pl_vulkan_hold_params hold{
            .tex = videoTarget, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .qf = queueFamily, .semaphore = {.sem = videoReady, .value = 0}};
        if (!pl_vulkan_hold_ex(vulkan->gpu, &hold)) {
            return false;
        }
        videoTargetHeld = true;
        videoReadyPending = true;
        return true;
    }

    bool UploadSourceTexture(const std::size_t index, const int width, const int height, const std::string_view formatName,
                             const std::span<const char> bytes, const int stride) {
        const pl_fmt format = pl_find_named_fmt(vulkan->gpu, formatName.data());
        if (!format || bytes.empty() || stride <= 0)
            return false;
        const pl_tex_params textureParameters{.w = width, .h = height, .format = format, .sampleable = true, .host_writable = true};
        if (!pl_tex_recreate(vulkan->gpu, &uploadTextures[index], &textureParameters))
            return false;
        const pl_tex_transfer_params transfer{.tex = uploadTextures[index],
                                              .row_pitch = static_cast<std::size_t>(stride),
                                              .ptr = const_cast<char*>(bytes.data())}; // NOLINT: synchronous libplacebo upload boundary
        return pl_tex_upload(vulkan->gpu, &transfer);
    }

    bool UploadYuv(const std::shared_ptr<RawImage>& image) {
        const auto format = image->Format();
        const int planeCount{format == kRawImageNV12 ? 2 : 3};
        pl_frame source{};
        source.num_planes = planeCount;
        for (int index{}; index < planeCount; ++index) {
            const auto layout = image->Layout(static_cast<std::size_t>(index));
            const auto bytes = image->Plane(static_cast<std::size_t>(index));
            if (!layout)
                return false;
            const bool chroma{index > 0};
            const bool subsampled{format == kRawImageI420 || format == kRawImageNV12};
            const int planeWidth{chroma && subsampled ? (image->img_width + 1) / 2 : image->img_width};
            const int planeHeight{chroma && subsampled ? (image->img_height + 1) / 2 : image->img_height};
            const bool interleaved{format == kRawImageNV12 && index == 1};
            if (!UploadSourceTexture(static_cast<std::size_t>(index), planeWidth, planeHeight, interleaved ? "rg8" : "r8", bytes, layout->stride)) {
                return false;
            }
            source.planes[index].texture = uploadTextures[static_cast<std::size_t>(index)];
            source.planes[index].components = interleaved ? 2 : 1;
            source.planes[index].component_mapping[0] = index == 0 ? 0 : 1;
            if (interleaved)
                source.planes[index].component_mapping[1] = 2;
            else if (index == 2)
                source.planes[index].component_mapping[0] = 2;
        }
        source.crop = {0.0F, 0.0F, static_cast<float>(image->img_width), static_cast<float>(image->img_height)};
        source.repr = pl_color_repr_hdtv;
        source.color = pl_color_space_bt709;
        return RenderSource(source);
    }

    void RecreateSwapchainIfNeeded() {
        if (!resizePending || pendingWidth <= 0 || pendingHeight <= 0)
            return;
        static_cast<void>(vkDeviceWaitIdle(vulkan->device));
        const std::uint32_t minimumImages{static_cast<std::uint32_t>(ImGui_ImplVulkanH_GetMinImageCountFromPresentMode(swapchain.PresentMode))};
        ImGui_ImplVulkan_SetMinImageCount(std::max(2U, minimumImages));
        ImGui_ImplVulkanH_CreateOrResizeWindow(instance->instance, vulkan->phys_device, vulkan->device, &swapchain, queueFamily, nullptr,
                                               pendingWidth, pendingHeight, std::max(2U, minimumImages), 0);
        swapchain.FrameIndex = 0;
        resizePending = false;
    }

    void RequestCurrentWindowResize() {
        SDL_GetWindowSizeInPixels(&window.get().Native(), &pendingWidth, &pendingHeight);
        resizePending = pendingWidth > 0 && pendingHeight > 0;
    }
};

std::expected<std::shared_ptr<VulkanRenderer>, std::string> VulkanRenderer::Create(const WindowHost& window) {
    auto renderer = std::make_shared<VulkanRenderer>(window);
    if (!renderer->Initialize())
        return std::unexpected{"Vulkan video device or presentation initialization failed"};
    return renderer;
}

VulkanRenderer::VulkanRenderer(const WindowHost& window) : impl_{std::make_unique<Impl>(window)} {}
VulkanRenderer::~VulkanRenderer() = default;

bool VulkanRenderer::Initialize() {
    const pl_log_params logParameters{.log_cb = PlaceboLog, .log_level = PL_LOG_WARN};
    impl_->log = pl_log_create(PL_API_VER, &logParameters);
    if (!impl_->log)
        return false;
    std::uint32_t extensionCount{};
    const auto extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount); // NOLINT: borrowed SDL extension array
    if (!extensions || extensionCount == 0U)
        return false;
    auto instanceParameters = pl_vk_inst_default_params;
    instanceParameters.extensions = extensions;
    instanceParameters.num_extensions = static_cast<int>(extensionCount);
    instanceParameters.get_proc_addr = vkGetInstanceProcAddr;
    impl_->instance = pl_vk_inst_create(impl_->log, &instanceParameters);
    if (!impl_->instance || !SDL_Vulkan_CreateSurface(&impl_->window.get().Native(), impl_->instance->instance, nullptr, &impl_->surface))
        return false;
    auto deviceParameters = pl_vulkan_default_params;
    deviceParameters.instance = impl_->instance->instance;
    deviceParameters.get_proc_addr = impl_->instance->get_proc_addr;
    deviceParameters.surface = impl_->surface;
    deviceParameters.extra_queues = VK_QUEUE_FLAG_BITS_MAX_ENUM;
    deviceParameters.opt_extensions = kOptionalDeviceExtensions.data();
    deviceParameters.num_opt_extensions = static_cast<int>(kOptionalDeviceExtensions.size());
    impl_->vulkan = pl_vulkan_create(impl_->log, &deviceParameters);
    if (!impl_->vulkan || !SupportsNativeVulkanVideo(impl_->vulkan) || !impl_->CreateDecoderDevice())
        return false;
    impl_->queueFamily = impl_->vulkan->queue_graphics.index;
    vkGetDeviceQueue(impl_->vulkan->device, impl_->queueFamily, 0, &impl_->queue);
    if (impl_->queue == VK_NULL_HANDLE || !impl_->CreateSwapchain())
        return false;
    impl_->videoRenderer = pl_renderer_create(impl_->log, impl_->vulkan->gpu);
    const VkSemaphoreCreateInfo semaphore{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    return impl_->videoRenderer &&
           VulkanSucceeded(vkCreateSemaphore(impl_->vulkan->device, &semaphore, nullptr, &impl_->videoReady), "create video-ready semaphore");
}

bool VulkanRenderer::InitializeImGuiBackend() {
    if (impl_->imguiBackendInitialized)
        return true;
    ImGui_ImplVulkan_InitInfo information{};
    information.ApiVersion = impl_->vulkan->api_version;
    information.Instance = impl_->instance->instance;
    information.PhysicalDevice = impl_->vulkan->phys_device;
    information.Device = impl_->vulkan->device;
    information.QueueFamily = impl_->queueFamily;
    information.Queue = impl_->queue;
    information.DescriptorPoolSize = 128;
    information.MinImageCount = std::max(2U, static_cast<std::uint32_t>(impl_->swapchain.ImageCount));
    information.ImageCount = static_cast<std::uint32_t>(impl_->swapchain.ImageCount);
    information.PipelineInfoMain.RenderPass = impl_->swapchain.RenderPass;
    information.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    impl_->imguiBackendInitialized = ImGui_ImplVulkan_Init(&information);
    return impl_->imguiBackendInitialized;
}

void VulkanRenderer::ShutdownImGuiBackend() {
    if (!impl_ || !impl_->imguiBackendInitialized)
        return;
    static_cast<void>(vkDeviceWaitIdle(impl_->vulkan->device));
    if (impl_->videoDescriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(impl_->videoDescriptor);
        impl_->videoDescriptor = VK_NULL_HANDLE;
    }
    ImGui_ImplVulkan_Shutdown();
    impl_->imguiBackendInitialized = false;
}

void VulkanRenderer::BeginImGuiFrame() const {
    ImGui_ImplVulkan_NewFrame();
}

bool VulkanRenderer::Resize(const int width, const int height) {
    impl_->pendingWidth = width;
    impl_->pendingHeight = height;
    impl_->resizePending = width > 0 && height > 0;
    return true;
}

bool VulkanRenderer::UpdateVideoTexture(const int width, const int height, const std::span<const std::uint8_t> bgra) {
    if (width <= 0 || height <= 0 || bgra.size() != static_cast<std::size_t>(width) * height * 4U || !impl_->EnsureVideoTarget(width, height))
        return false;
    const auto bytes = std::as_bytes(bgra);
    if (!impl_->UploadSourceTexture(0, width, height, "bgra8", {reinterpret_cast<const char*>(bytes.data()), bytes.size()}, width * 4))
        return false;
    pl_frame source{};
    source.num_planes = 1;
    source.planes[0].texture = impl_->uploadTextures[0];
    source.planes[0].components = 4;
    for (int index{}; index < 4; ++index)
        source.planes[0].component_mapping[index] = index;
    source.crop = {0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)};
    source.repr = pl_color_repr_rgb;
    source.color = pl_color_space_srgb;
    return impl_->RenderSource(source);
}

bool VulkanRenderer::UpdateVideoFrame(const std::shared_ptr<RawImage>& image) {
    if (!image || !impl_->EnsureVideoTarget(image->img_width, image->img_height))
        return false;
    if (image->Format() == kRawImageVulkanAVFrame) {
        const auto frame = VulkanFrameOf(image);
        if (!frame || !frame->frame || !av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->frame->format)))
            return false;
        pl_frame source{};
        pl_avframe_params parameters{};
        parameters.frame = frame->frame.get(); // NOLINT: synchronous libplacebo borrowed frame boundary
        parameters.tex = impl_->mappedTextures.data();
        if (!pl_map_avframe_ex(impl_->vulkan->gpu, &source, &parameters))
            return false;
        const bool rendered{impl_->RenderSource(source)};
        pl_unmap_avframe(impl_->vulkan->gpu, &source);
        return rendered;
    }
    if (image->Format() == kRawImageNV12 || image->Format() == kRawImageI420 || image->Format() == kRawImageI444)
        return impl_->UploadYuv(image);
    if (image->Format() == kRawImageD3D11Texture) {
        const auto d3dFrame = D3D11FrameOf(image);
        const auto downloaded = d3dFrame ? DownloadD3dNv12Frame(*d3dFrame) : nullptr;
        if (!downloaded)
            return false;
        LOGW("Vulkan decoder was unavailable; presenting D3D11VA fallback through a YUV-only transfer");
        return impl_->UploadYuv(downloaded);
    }
    return false;
}

std::uint64_t VulkanRenderer::VideoTextureId() const noexcept {
    return reinterpret_cast<std::uint64_t>(impl_->videoDescriptor);
}

AvBufferPtr VulkanRenderer::ShareDecoderDevice() {
    if (!impl_->decoderDevice)
        return {};
    struct DeviceLease final {
        std::shared_ptr<VulkanRenderer> renderer{};
        AvBufferPtr buffer{};
    };
    const auto lease = std::make_shared<DeviceLease>(shared_from_this(), CloneAvBuffer(*impl_->decoderDevice));
    return lease->buffer ? AvBufferPtr{lease, lease->buffer.get()} : AvBufferPtr{};
}

void VulkanRenderer::Render() {
    impl_->RecreateSwapchainIfNeeded();
    auto& window = impl_->swapchain;
    const VkSemaphore acquired{window.FrameSemaphores[window.SemaphoreIndex].ImageAcquiredSemaphore};
    const VkSemaphore completed{window.FrameSemaphores[window.SemaphoreIndex].RenderCompleteSemaphore};
    const VkResult acquisition{vkAcquireNextImageKHR(impl_->vulkan->device, window.Swapchain, std::numeric_limits<std::uint64_t>::max(), acquired,
                                                     VK_NULL_HANDLE, &window.FrameIndex)};
    if (acquisition == VK_ERROR_OUT_OF_DATE_KHR || acquisition == VK_SUBOPTIMAL_KHR) {
        impl_->RequestCurrentWindowResize();
        return;
    }
    if (!VulkanSucceeded(acquisition, "acquire swapchain image"))
        return;
    auto& frame = window.Frames[window.FrameIndex];
    if (!VulkanSucceeded(vkWaitForFences(impl_->vulkan->device, 1, &frame.Fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max()),
                         "wait for UI frame") ||
        !VulkanSucceeded(vkResetFences(impl_->vulkan->device, 1, &frame.Fence), "reset UI frame fence") ||
        !VulkanSucceeded(vkResetCommandPool(impl_->vulkan->device, frame.CommandPool, 0), "reset UI command pool")) {
        return;
    }
    const VkCommandBufferBeginInfo begin{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (!VulkanSucceeded(vkBeginCommandBuffer(frame.CommandBuffer, &begin), "begin UI command buffer"))
        return;
    VkRenderPassBeginInfo renderPass{};
    renderPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPass.renderPass = window.RenderPass;
    renderPass.framebuffer = frame.Framebuffer;
    renderPass.renderArea.extent = {static_cast<std::uint32_t>(window.Width), static_cast<std::uint32_t>(window.Height)};
    renderPass.clearValueCount = 1;
    renderPass.pClearValues = &window.ClearValue;
    vkCmdBeginRenderPass(frame.CommandBuffer, &renderPass, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.CommandBuffer);
    vkCmdEndRenderPass(frame.CommandBuffer);
    if (!VulkanSucceeded(vkEndCommandBuffer(frame.CommandBuffer), "end UI command buffer"))
        return;

    std::array<VkSemaphore, 2> waitSemaphores{acquired, impl_->videoReady};
    std::array<VkPipelineStageFlags, 2> waitStages{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = impl_->videoReadyPending ? 2U : 1U;
    submit.pWaitSemaphores = waitSemaphores.data();
    submit.pWaitDstStageMask = waitStages.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.CommandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &completed;
    impl_->vulkan->lock_queue(impl_->vulkan, impl_->queueFamily, 0);
    const VkResult submitted{vkQueueSubmit(impl_->queue, 1, &submit, frame.Fence)};
    impl_->vulkan->unlock_queue(impl_->vulkan, impl_->queueFamily, 0);
    if (!VulkanSucceeded(submitted, "submit UI frame"))
        return;
    impl_->lastSubmittedFence = frame.Fence;
    impl_->videoReadyPending = false;

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &completed;
    present.swapchainCount = 1;
    present.pSwapchains = &window.Swapchain;
    present.pImageIndices = &window.FrameIndex;
    impl_->vulkan->lock_queue(impl_->vulkan, impl_->queueFamily, 0);
    const VkResult presented{vkQueuePresentKHR(impl_->queue, &present)};
    impl_->vulkan->unlock_queue(impl_->vulkan, impl_->queueFamily, 0);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
        impl_->RequestCurrentWindowResize();
    else
        static_cast<void>(VulkanSucceeded(presented, "present UI frame"));
    window.SemaphoreIndex = (window.SemaphoreIndex + 1U) % window.SemaphoreCount;
}

} // namespace px::desktop
