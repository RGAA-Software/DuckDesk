#include "dda_capture.h"
#include <algorithm>
#include <iostream>
#include <timeapi.h>
#include <functional>
#include <chrono>
#include "px_common_new/string_util.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"
#include "px_common_new/monitors.h"
#include "px_common_new/thread.h"
#include "px_capture_new/capture_message.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "dda_capture_plugin.h"
#include "px_common_new/win32/d3d_debug_helper.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "Winmm.lib")

#define S_NOT_CHANGED ((HRESULT)5L)

namespace px
{

    DDACapture::DDACapture(DDACapturePlugin* plugin, const CaptureMonitorInfo& my_monitor_info)
        : DesktopCaptureSource(my_monitor_info) {
        plugin_ = plugin;
        fps_stat_ = std::make_shared<FpsStat>();
        LOGI("DDACapture my monitor info: {}", my_monitor_info.Dump());
    }

    DDACapture::~DDACapture() {
        StopCapture();
    }

    bool DDACapture::Init() {
        const int kInitTryMaxCount = 3;
        int try_count = -1;
        bool dda_init_res = false;

        do {
            ++try_count;
            dda_init_res = this->InitInternal();
            if (!dda_init_res) {
                LOGE("dda capture init failed for target: {}, will try again.", my_monitor_info_.name_);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            else {
                break;
            }

        } while (try_count < kInitTryMaxCount);

        // test init failed begin //
        //dda_init_res = false;
        // test init failed end //

        LOGI("Init DDA result: {} -> {}", my_monitor_info_.name_, dda_init_res);
        return dda_init_res;
    }

    bool DDACapture::InitInternal() {
        HRESULT res = 0;
        int adapter_index = 0;

        ComPtr<IDXGIFactory1> factory1 = nullptr;
        res = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)factory1.GetAddressOf());
        if (res != S_OK) {
            LOGE("CreateDXGIFactory1 failed");
            return false;
        }
        do {
            ComPtr<IDXGIAdapter1> adapter1 = nullptr;
            ComPtr<ID3D11Device> d3d11_device = nullptr;
            ComPtr<ID3D11DeviceContext> d3d11_device_context = nullptr;

            res = factory1->EnumAdapters1(adapter_index, adapter1.GetAddressOf());
            if (res != S_OK) {
                LOGE("EnumAdapters1 failed, index: {}", adapter_index);
                break;
            }
            D3D_FEATURE_LEVEL feature_level;
            DXGI_ADAPTER_DESC adapter_desc{};
            adapter1->GetDesc(&adapter_desc);
            LOGI("Adapter Index:{} Name:{}", adapter_index, StringUtil::ToUTF8(adapter_desc.Description).c_str());
            auto adapter_uid = adapter_desc.AdapterLuid.LowPart;
            res = D3D11CreateDevice(adapter1.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    nullptr, 0, D3D11_SDK_VERSION, d3d11_device.GetAddressOf(), &feature_level, d3d11_device_context.GetAddressOf());
            if (res != S_OK || !d3d11_device) {
                LOGE("D3D11CreateDevice failed: {}", StringUtil::GetErrorStr(res).c_str());
                break;
            }
            if (feature_level < D3D_FEATURE_LEVEL_11_0) {
                LOGE("D3D11CreateDevice returns an instance without DirectX 11 support, level : {}  Following initialization may fail",(int) feature_level);
                break;
            }
            ComPtr<IDXGIDevice> dxgi_device;
            res = d3d11_device->QueryInterface(dxgi_device.GetAddressOf());
            if (res != S_OK || !dxgi_device) {
                LOGE("ID3D11Device is not an implementation of IDXGIDevice, this usually means the system does not support DirectX 11. Error:{}, code: {}",
                     StringUtil::GetErrorStr(res), res);
                break;
            }

            int monitor_index = -1;
            do {
                ++monitor_index;
                ComPtr<IDXGIOutput> output;
                res = adapter1->EnumOutputs(monitor_index, output.GetAddressOf());
                if (res == DXGI_ERROR_NOT_FOUND) {
                    LOGE("adapter1->EnumOutputs return DXGI_ERROR_NOT_FOUND,Please Check RDP connect.");
                    break;
                }
                if (res == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
                    LOGE("IDXGIAdapter::EnumOutputs returns NOT_CURRENTLY_AVAILABLE. This may happen when running in session 0");
                    break;
                }
                if (res != S_OK || !output) {
                    LOGE("IDXGIAdapter::EnumOutputs returns an unexpected result {} with error code {}",
                         StringUtil::GetErrorStr(res).c_str(), res);
                    continue;
                }
                DXGI_OUTPUT_DESC output_desc{};
                res = output->GetDesc(&output_desc);
                if (res == S_OK) {
                    auto dev_name = StringUtil::ToUTF8(output_desc.DeviceName);
                    if (dev_name != my_monitor_info_.name_) {
                        LOGW("My device name is :{}, but your name is : {}, continue to find.", my_monitor_info_.name_, dev_name);
                        continue;
                    }
                    LOGI("Yes, found the same device: {}", dev_name);

                    dxgi_output_duplication_.output_desc_ = output_desc;
                    dxgi_output_duplication_.monitor_win_info_ = my_monitor_info_;

                    auto func_valid_rect = [](const RECT &rect) -> bool {
                        return rect.right > rect.left && rect.bottom > rect.top;
                    };

                    bool is_valid_rect = func_valid_rect(output_desc.DesktopCoordinates);
                    LOGI("AttachedToDesktop: {}, is valid rect: {}", output_desc.AttachedToDesktop, is_valid_rect);
                    if (output_desc.AttachedToDesktop && is_valid_rect) {
                        ComPtr<IDXGIOutput1> output1;
                        res = output->QueryInterface(output1.GetAddressOf());
                        if (res != S_OK || !output1) {
                            LOGE("Failed to convert IDXGIOutput to IDXGIOutput1, this usually means the system does not support DirectX 11");
                            continue;
                        }

                        bool init_dda_success = false;
                        static const int max_retry_count = 5;
                        for (int j = 0; j < max_retry_count; ++j) {
                            HRESULT error = output1->DuplicateOutput(d3d11_device.Get(), dxgi_output_duplication_.duplication_.GetAddressOf());
                            // to see : https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput
                            if (error != S_OK || !dxgi_output_duplication_.duplication_) {
                                if (error == E_UNEXPECTED) {
                                    LOGE("DuplicateOutput E_UNEXPECTED");
                                }
                                else if (error == E_ACCESSDENIED) {
                                    const ACCESS_MASK ac = GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE;
                                    HDESK desk = ::OpenInputDesktop(0, 0, ac);
                                    if (desk) {
                                        auto r = SetThreadDesktop(desk);
                                        LOGW("SetThreadDesktop, r: {}", r);
                                    }
                                    CloseDesktop(desk);
                                    continue;
                                }
                                else if (error == E_INVALIDARG) {
                                    LOGE("!! invalid args !!");
                                }
                                LOGE("Failed to duplicate output from IDXGIOutput1, error {} with code {:x}",
                                     StringUtil::GetErrorStr(error).c_str(), (uint32_t) error);
                                continue;
                            } else {
                                dxgi_output_duplication_.output1_ = output1;
                                init_dda_success = true;
                                LOGI("Init DDA mode success, monitor_index: {}, monitor_name: {}", monitor_index, my_monitor_info_.name_);
                                break;
                            }
                        }
                        if (init_dda_success) {
                            break;
                        }
                    } else {
                        std::stringstream ss;
                        ss << (output_desc.AttachedToDesktop ? "Attached" : "Detached")
                           << " output " << monitor_index << " ("
                           << output_desc.DesktopCoordinates.top << ", "
                           << output_desc.DesktopCoordinates.left << ") - ("
                           << output_desc.DesktopCoordinates.bottom << ", "
                           << output_desc.DesktopCoordinates.right << ") is ignored.";
                        LOGI("MonitorInfo invalid: {}", ss.str());
                    }
                } else {
                    LOGE("Failed to get output description of device :{}", monitor_index);
                }
            } while (true);

            if (!dxgi_output_duplication_.duplication_) {
                adapter_index++;
                continue;
            }
            last_list_texture_ = std::make_shared<SharedD3d11Texture2D>();
            d3d11_device_ = d3d11_device;
            d3d11_device_context_ = d3d11_device_context;
            break;

        } while(true);

        if (!dxgi_output_duplication_.duplication_) {
            LOGI("Init DDA failed.");
            if (last_list_texture_) {
                last_list_texture_->Exit();
                last_list_texture_ = nullptr;
            }
            if (d3d11_device_) {
                //d3d11_device_.Release();
                d3d11_device_.Reset();
            }
            if (d3d11_device_context_) {
                //d3d11_device_context_.Release();
                d3d11_device_context_.Reset();
            }
            return false;
        }

        std::vector<MonitorWinInfo> win_monitors = EnumerateAllMonitors();
        for (const auto& info : win_monitors) {
            if (info.name_ == my_monitor_info_.name_ && info.is_primary_) {
                is_primary_monitor_ = true;
            }
        }

        LOGI("Init DDA successful");
        return true;
    }

    bool DDACapture::IsInitSuccess() {
        return dxgi_output_duplication_.duplication_ != nullptr;
    }

    bool DDACapture::Exit() {
        if (cached_texture_) {
            //cached_texture_.Release();
            cached_texture_.Reset();
        }
        if (last_list_texture_) {
            last_list_texture_->Exit();
            last_list_texture_ = nullptr;
        }
        dxgi_output_duplication_.Exit();
        // d3d11_device_.Release();
        d3d11_device_.Reset();
        // d3d11_device_context_.Release();
        d3d11_device_context_.Reset();
        return true;
    }

    HRESULT DDACapture::CaptureNextFrame(int wait_time, ComPtr<ID3D11Texture2D>& out_tex) {
        DXGI_OUTDUPL_FRAME_INFO info;
        ComPtr<IDXGIResource> resource;
        ComPtr<ID3D11Texture2D> source;
        HRESULT res;
        if (!dxgi_output_duplication_.duplication_) {
            if (!Init()) {
                LOGE("Capture dxgi init failed!");
                return S_FALSE;
            }
            if (!dxgi_output_duplication_.duplication_) {
                return S_FALSE;
            }
        }
        // 标准 DDA 协议:acquire 成功 -> 拷贝完 -> 立刻 ReleaseFrame。
        // 不要在 AcquireNextFrame 之前释放上一帧——提前释放会在高频变化(拖动窗口)时
        // 让 DDA 丢帧,导致采集从 60fps 崩到 ~10fps(见 dxgi_capture_probe 对照验证)。
        res = dxgi_output_duplication_.duplication_->AcquireNextFrame(wait_time, &info, resource.GetAddressOf());
        if (res != S_OK) {
            return res;  // WAIT_TIMEOUT/错误:未取得帧,无需 ReleaseFrame
        }
        res = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **) source.GetAddressOf());
        if (res != S_OK) {
            LOGE("QueryInterface failed when capturing: {}", StringUtil::GetErrorStr(res));
            dxgi_output_duplication_.duplication_->ReleaseFrame();
            return res;
        }
        if (info.AccumulatedFrames == 0) {
            // 桌面未变化:立即释放,不上送
            dxgi_output_duplication_.duplication_->ReleaseFrame();
            return S_NOT_CHANGED;
        }
        out_tex = source;
        // 帧未在此释放:由 OnCaptureFrame 拷贝完成后调用 ReleaseFrame
        return res;
    }

    void DDACapture::Capture() {
        // [LAT-capture] 采集节奏诊断窗口(每 5s 汇总一次)
        auto lat_win_beg = std::chrono::steady_clock::now();
        uint64_t lat_acquire_new = 0;       // AcquireNextFrame 返回 S_OK(新帧)
        uint64_t lat_acquire_timeout = 0;   // 返回 DXGI_ERROR_WAIT_TIMEOUT(桌面无新帧)
        uint64_t lat_acquire_nochange = 0;  // 返回 S_NOT_CHANGED(AccumulatedFrames==0)
        uint64_t lat_acquire_err = 0;       // 返回 S_FALSE/ACCESS_LOST/INVALID_CALL(出错重初始化)
        uint64_t lat_backpressure = 0;      // 网络队列积压导致的采集跳过
        uint64_t lat_copy_us_sum = 0;       // OnCaptureFrame(两次 CopyResource+送帧)耗时累计
        uint64_t lat_copy_us_max = 0;
        uint64_t lat_copy_cnt = 0;

        while (!stop_flag_) {
            // [LAT-capture] 每 5s 汇总:新帧 vs 超时、采集帧间隔分布
            {
                auto now = std::chrono::steady_clock::now();
                auto el_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - lat_win_beg).count();
                if (el_ms >= 5000) {
                    auto gaps = capture_gaps_.ToVector();
                    int32_t gmin = 100000;
                    int32_t gmax = 0;
                    int64_t gsum = 0;
                    for (auto g : gaps) {
                        if (g < gmin) { gmin = g; }
                        if (g > gmax) { gmax = g; }
                        gsum += g;
                    }
                    double gavg = gaps.empty() ? 0.0 : (double)gsum / (double)gaps.size();
                    LOGI("[LAT-capture] mon={} win={}ms new={} timeout={} nochange={} err={} backpressure={} fps={} gap_min={} gap_avg={:.1f} gap_max={} copy_avg_us={} copy_max_us={}",
                         my_monitor_info_.name_, el_ms, lat_acquire_new, lat_acquire_timeout,
                         lat_acquire_nochange, lat_acquire_err, lat_backpressure,
                         fps_stat_->value(), gmin, gavg, gmax,
                         lat_copy_cnt > 0 ? (lat_copy_us_sum / lat_copy_cnt) : 0, lat_copy_us_max);
                    lat_win_beg = now;
                    lat_acquire_new = 0;
                    lat_acquire_timeout = 0;
                    lat_acquire_nochange = 0;
                    lat_acquire_err = 0;
                    lat_backpressure = 0;
                    lat_copy_us_sum = 0;
                    lat_copy_us_max = 0;
                    lat_copy_cnt = 0;
                }
            }

            // process tasks
            {
                auto tasks = tasks_.Clone();
                tasks_.Clear();
                for (const auto& task : tasks) {
                    task();
                }
            }

            if (pausing_ || !d3d11_device_ || !d3d11_device_context_ /*|| plugin_->DontHaveConnectedClientsNow()*/) {
                std::this_thread::sleep_for(std::chrono::milliseconds(17));
                continue;
            }

            // test beg
            const auto queuing_msg_count =
                plugin_->GetNetworkMediaBacklog();
            if (queuing_msg_count >= 10) {
                ++lat_backpressure;
                TimeUtil::DelayBySleep(1);
                LOGW("too many queuing messages, ignore this capturing loop, count: {}", queuing_msg_count);
                continue;
            }
            // test end

            // 向上取整:1000/60 整数除法会截断成 16ms,而 60Hz 实际是 16.67ms,
            // AcquireNextFrame(16ms) 会在桌面下一帧到来前就超时,高动态(拖动)时累积丢帧。
            auto target_duration = (1000 + capture_fps_ - 1) / capture_fps_;
            //LOGI("target_duration: {}, capture_fps_: {}", target_duration, capture_fps_);
            ComPtr<ID3D11Texture2D> texture = nullptr;
            // do capture
            auto res = CaptureNextFrame(target_duration, texture);

            // test timeout beg //
            //res = DXGI_ERROR_WAIT_TIMEOUT;
            // test timeout end //

            bool is_cached = false;
            if (res == S_OK) {
                ++lat_acquire_new;
                // fps tick
                fps_stat_->Tick();

                // capture gaps
                auto curr_timestamp = (int64_t)TimeUtil::GetCurrentTimestamp();
                if (last_captured_timestamp_ == 0) {
                    last_captured_timestamp_ = curr_timestamp;
                }
                auto diff = curr_timestamp - last_captured_timestamp_;
                if (capture_gaps_.Size() >= 180) {
                    capture_gaps_.PopFront();
                }
                capture_gaps_.PushBack((int32_t)diff);
                last_captured_timestamp_ = curr_timestamp;
            }
            else if (res == S_FALSE || res == DXGI_ERROR_ACCESS_LOST || res == DXGI_ERROR_INVALID_CALL) {
                ++lat_acquire_err;
                LOGE("CaptureNextFrame, monitor: {}, err: {:x}, duplicate retry? : {}", my_monitor_info_.name_, (uint32_t)res, dxgi_output_duplication_.has_retry_);
                if (res == DXGI_ERROR_ACCESS_LOST && dxgi_output_duplication_.output1_ && !dxgi_output_duplication_.has_retry_) {
                    dxgi_output_duplication_.has_retry_ = true;
                    HRESULT error = dxgi_output_duplication_.output1_->DuplicateOutput(d3d11_device_.Get(), dxgi_output_duplication_.duplication_.GetAddressOf());
                    if (error == S_OK) {
                        LOGW("Re-DuplicateOutput success, will continue to try capturing.");
                        continue;
                    }
                    else {
                        LOGW("Re-DuplicateOutput failed, will Re-Init.");
                    }
                }
                LOGE("CaptureNextFrame Re-Init, name = {}, err: {:x}, msg: {}", my_monitor_info_.name_, (uint32_t)res, StringUtil::GetErrorStr(res));
                Exit();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                auto init_ok = Init();
                if (!init_ok) {
                    LOGE("ReInit failed, can't capture.");
                    LOGI("Will check this monitor exists or not");
                    if (!IsThisMonitorExist()) {
                        LOGI("This monitor: {} is not exist now!", my_monitor_info_.name_);
                        break;
                    }
                    if (err_callback_) {
                        err_callback_(MonitorCaptureError::kCantCapture);
                    }
                }
                else {
                    used_cache_times_ = 0;
                    refresh_screen_ = true;
                    LOGE("ReInit successfully.");
                }
                continue;
            }
            else if (res == DXGI_ERROR_WAIT_TIMEOUT || res == S_NOT_CHANGED) {
                if (res == DXGI_ERROR_WAIT_TIMEOUT) {
                    ++lat_acquire_timeout;
                }
                else {
                    ++lat_acquire_nochange;
                }
                //LOGI("CaptureNextFrame res: {:x}", (uint32_t)res);
                 if (refresh_screen_) {
                     if (cached_texture_ == nullptr) {
                         continue;
                     }
                     if (used_cache_times_++ > 5) {
                         refresh_screen_ = false;
                     }
                     texture = cached_texture_;
                     is_cached = true;

                     LOGI("Use cached texture!");
                 }
                 else {
                     continue;
                 }
            }

            if (texture) {
                continuous_timeout_times_ = 0;
                auto copy_beg = std::chrono::steady_clock::now();
                OnCaptureFrame(texture, is_cached);
                auto copy_us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - copy_beg).count();
                ++lat_copy_cnt;
                lat_copy_us_sum += copy_us;
                if (copy_us > lat_copy_us_max) { lat_copy_us_max = copy_us; }
                // 不主动配速睡眠:AcquireNextFrame 的超时本身就是节拍器。
            }
        }
    }

    void DDACapture::OnCaptureFrame(const ComPtr<ID3D11Texture2D>& texture, bool is_cached) {
        HRESULT result;
        // input texture info
        D3D11_TEXTURE2D_DESC input_desc;
        texture->GetDesc(&input_desc);
        UINT input_width = input_desc.Width;
        UINT input_height = input_desc.Height;
        DXGI_FORMAT input_format = input_desc.Format;

        //LOGI("OnCaptureFrame texture, format: {}", (int)input_format); // DXGI_FORMAT_B8G8R8A8_UNORM

        // shared texture info if exists
        UINT shared_width = 0;
        UINT shared_height = 0;
        DXGI_FORMAT shared_format = DXGI_FORMAT_UNKNOWN;
        auto shared_texture = last_list_texture_->texture2d_;
        if (shared_texture) {
            D3D11_TEXTURE2D_DESC shared_desc;
            shared_texture->GetDesc(&shared_desc);
            shared_width = shared_desc.Width;
            shared_height = shared_desc.Height;
            shared_format = shared_desc.Format;
        }

        bool texture_changed = (input_width != shared_width)
                || (input_height != shared_height)
                || (input_format != shared_format);

        if (texture_changed) {
            LOGI("texture changed, origin: {}x{}, format: {}", shared_width, shared_height, (int)shared_format);
            LOGI("texture changed, current: {}x{}, format: {}", input_width, input_height, (int)input_format);
            if (shared_texture) {
                //shared_texture.Release();
                shared_texture.Reset();
                last_list_texture_->texture2d_ = nullptr;
            }
            D3D11_TEXTURE2D_DESC create_desc;
            ZeroMemory(&create_desc, sizeof(create_desc));
            create_desc.Format = input_format;
            create_desc.Width = input_width;
            create_desc.Height = input_height;
            create_desc.MipLevels = 1;
            create_desc.ArraySize = 1;
            create_desc.SampleDesc.Count = 1;
            create_desc.Usage = D3D11_USAGE_DEFAULT;
            create_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            //create_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
            create_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

            result = d3d11_device_->CreateTexture2D(&create_desc, nullptr, last_list_texture_->texture2d_.GetAddressOf());
            if (FAILED(result)) {
                LOGE("desktop capture create texture failed with:{}", StringUtil::GetErrorStr(result).c_str());
                return;
            }

            ComPtr<IDXGIResource> dxgiResource;
            result = last_list_texture_->texture2d_->QueryInterface(__uuidof(IDXGIResource), (void**)dxgiResource.GetAddressOf());
            //result = last_list_texture_->texture2d_.As<IDXGIResource>(&dxgiResource);
            if (FAILED(result)) {
                LOGE("desktop capture as IDXGIResource failed with:{}", StringUtil::GetErrorStr(result).c_str());
                return;
            }
            HANDLE handle;
            result = dxgiResource->GetSharedHandle(&handle);
            if (FAILED(result)) {
                LOGI("desktop capture get shared handle failed with:{}", StringUtil::GetErrorStr(result).c_str());
                return;
            }
            last_list_texture_->shared_handle_ = handle;

            // cached textures
            if (cached_texture_ != nullptr) {
                //cached_texture_.Release();
                //cached_texture_ = nullptr;
                cached_texture_.Reset();
            }
            ComPtr<ID3D11Texture2D> cached_texture;
            result = d3d11_device_->CreateTexture2D(&create_desc, nullptr, cached_texture.GetAddressOf());
            if (FAILED(result)) {
                LOGE("create cached texture failed with:{}", StringUtil::GetErrorStr(result).c_str());
                return;
            }
            cached_texture_ = cached_texture;
            LOGI("Create cached texture success: {}", my_monitor_info_.name_);
        }

        //ComPtr<IDXGIKeyedMutex> keyMutex;
        //result = last_list_texture_.texture2d_.As<IDXGIKeyedMutex>(&keyMutex);
        //if (FAILED(result)) {
        //    LOGE("desktop frame capture as IDXGIKeyedMutex failed:{}", StringUtil::GetErrorStr(result).c_str());
        //    return;
        //}
        //result = keyMutex->AcquireSync(0x0, 17/*INFINITE*/);
        //if (FAILED(result)) {
        //    LOGE("desktop frame capture texture AcquireSync failed with:{}", StringUtil::GetErrorStr(result).c_str());
        //    return;
        //}

        d3d11_device_context_->CopyResource(last_list_texture_->texture2d_.Get(), texture.Get());
        if (!is_cached) {
            // [ISOLATION-TEST] 临时去掉缓存纹理的第二次 CopyResource,只保留共享纹理拷贝+送帧,
            // 用于隔离「每帧双 GPU 拷贝」是否是拖动时 DDA 采集崩到 10fps 的原因。
            // 拷贝完立刻释放 DDA 帧(标准协议),让下一帧能被及时取到
            if (dxgi_output_duplication_.duplication_) {
                dxgi_output_duplication_.duplication_->ReleaseFrame();
            }
        }

        //if (SUCCEEDED(result) && keyMutex) {
        //    keyMutex->ReleaseSync(0x0);
        //}

        if (plugin_->IsPluginEnabled()) {
            bool request_idr = is_cached;
            SendTextureHandle(last_list_texture_->shared_handle_, input_width, input_height, input_format, request_idr);
        }
    }

    void DDACapture::SendCachedTexture() {
        auto task = [=, this]() {
            if (cached_texture_ && last_list_texture_&& last_list_texture_->texture2d_) {
                OnCaptureFrame(cached_texture_, true);
            }
        };
        tasks_.PushBack(task);
    }

    void DDACapture::SendTextureHandle(const HANDLE &shared_handle, uint32_t width, uint32_t height, DXGI_FORMAT format, bool request_idr) {
        CaptureVideoFrame cap_video_frame{};
        cap_video_frame.type_ = kCaptureVideoFrame;
        cap_video_frame.capture_type_ = kCaptureVideoByHandle;
        cap_video_frame.data_length = 0;
        cap_video_frame.frame_width_ = width;
        cap_video_frame.frame_height_ = height;
        cap_video_frame.frame_index_ = GetFrameIndex();
        cap_video_frame.handle_ = reinterpret_cast<uint64_t>(shared_handle);
        cap_video_frame.frame_format_ = format;
        cap_video_frame.adapter_uid_ = my_monitor_info_.adapter_uid_;
        cap_video_frame.request_idr_ = request_idr;
        auto mon_index_res = plugin_->GetMonIndexByName(my_monitor_info_.name_);
        if (mon_index_res.has_value()) {
            cap_video_frame.monitor_index_ = mon_index_res.value();
        }
        else {
            LOGE("desktop capture get mon index by name failed!");
        }
        auto mon_win_info = dxgi_output_duplication_.monitor_win_info_;
        if (mon_win_info.Valid()) {
            if (StringUtil::CopyCStringToArray(cap_video_frame.display_name_, mon_win_info.name_)) {
                LOGW("display_name truncated for monitor: {}, src_len: {}, dst_len: {}",
                     mon_win_info.name_, mon_win_info.name_.size(), sizeof(cap_video_frame.display_name_));
            }
            cap_video_frame.left_ = mon_win_info.left_;
            cap_video_frame.top_ = mon_win_info.top_;
            cap_video_frame.right_ = mon_win_info.right_;
            cap_video_frame.bottom_ = mon_win_info.bottom_;
        }
        auto event = std::make_shared<PxPluginCapturedVideoFrameEvent>();
        event->frame_ = cap_video_frame;
        this->plugin_->CallbackEvent(event);

    }

    int64_t DDACapture::GetFrameIndex() {
        monitor_frame_index_++;
        return monitor_frame_index_;
    }

    bool DDACapture::StartCapture() {
        auto task = [this] {
            Capture();
        };

        capture_thread_ = Thread::MakeOnceTask(task, std::format("dda_capture:{}", my_monitor_info_.name_), false);
        return true;
    }

    bool DDACapture::PauseCapture() {
        pausing_ = true;
        return true;
    }

    void DDACapture::ResumeCapture() {
        pausing_ = false;
        plugin_->InsertIdr();
    }

    void DDACapture::StopCapture() {
        stop_flag_ = true;
        if (capture_thread_) {
            capture_thread_->Exit();
            // Capture() can still be inside AcquireNextFrame/OnCaptureFrame here.
            // Releasing the duplication or D3D objects before that thread exits
            // races with topology rebuilds and can leave the replacement capture
            // permanently timing out (or crash in the driver/runtime).
            if (capture_thread_->IsJoinable()) {
                capture_thread_->Join();
            }
            capture_thread_ = nullptr;
        }
        this->Exit();
    }

    void DDACapture::RefreshScreen() {
        DesktopCaptureSource::RefreshScreen();
        used_cache_times_ = 0;
    }

    bool DDACapture::IsPrimaryMonitor() {
        return is_primary_monitor_;
    }

    int DDACapture::GetCapturingFps() {
        return fps_stat_->value();
    }

    void DDACapture::TryWakeOs() {
        DesktopCaptureSource::TryWakeOs();
        if (d3d11_device_context_) {
            d3d11_device_context_->Flush();
        }
    }

    int32_t DDACapture::GetContinuousTimeoutTimes() {
        return continuous_timeout_times_.load();
    }

    void DDACapture::On16MilliSecond() {

    }

    void DDACapture::On33MilliSecond() {

    }

    void DDACapture::SetDDAErrorCallback(CaptureErrorCallback&& cbk){
        err_callback_ = std::move(cbk);
    }

} // tc
