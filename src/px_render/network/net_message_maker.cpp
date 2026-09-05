//
// Created by RGAA on 2023-12-25.
//

#include "net_message_maker.h"
#include "px_common/data.h"
#include "px_common/string_util.h"
#include "px_message.pb.h"
#include "px_common/key_helper.h"
#include "px_common/hardware.h"
#include "px_render/modules/render_module_registry.h"
#include "px_render/modules/module_ids.h"
#include "rd_app.h"
#include "rd_statistics.h"
#include "architecture/sources/monitor_capture_source.h"
#include "architecture/encoders/video_encoder_module.h"
#include "px_common/num_formatter.h"
#include "px_message/proto_converter.h"

#include <Windows.h>

namespace px
{
    static std::string GetWindowsProductName() {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t buf[256] = {0};
            DWORD size = sizeof(buf);
            if (RegQueryValueExW(hKey, L"ProductName", NULL, NULL, (LPBYTE)buf, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                return StringUtil::ToUTF8(buf);
            }
            RegCloseKey(hKey);
        }
        return "Windows";
    }



    std::string NetMessageMaker::MakeHelloMsg() {
        auto msg = std::make_shared<Message>();
        msg->set_type(px::kHello);
        auto hello = new Hello();
        msg->set_allocated_hello(hello);
        return msg->SerializeAsString();
    }

    std::string NetMessageMaker::MakeAckMsg() {
        auto msg = std::make_shared<Message>();
        msg->set_type(px::kAck);
        auto ack = new Ack();
        msg->set_allocated_ack(ack);
        return msg->SerializeAsString();
    }

    std::string NetMessageMaker::MakeHeartBeatMsg() {
        auto msg = std::make_shared<Message>();
        msg->set_type(px::kHeartBeat);
        auto heart_beat = new HeartBeat();
        msg->set_allocated_heartbeat(heart_beat);
        return msg->SerializeAsString();
    }

    std::shared_ptr<Data> NetMessageMaker::MakeOnHeartBeatMsg(const std::shared_ptr<RdApplication>& app, uint64_t index, int64_t timestamp) {
        auto stat = RdStatistics::Instance();

        auto msg = std::make_shared<Message>();
        msg->set_type(px::kOnHeartBeat);
        auto hb = msg->mutable_on_heartbeat();
        hb->set_index(index);
        hb->set_caps_lock_pressed(KeyHelper::IsCapsLockPressed());
        hb->set_caps_lock_state(KeyHelper::GetCapsLockState());
        hb->set_num_lock_pressed(KeyHelper::IsNumLockPressed());
        hb->set_num_lock_state(KeyHelper::GetNumLockState());
        hb->set_alt_pressed(KeyHelper::IsAltPressed());
        hb->set_control_pressed(KeyHelper::IsControlPressed());
        hb->set_win_pressed(KeyHelper::IsWinPressed());
        hb->set_shift_pressed(KeyHelper::IsShiftPressed());
        hb->set_timestamp(timestamp);

        if (auto capture_source = app->GetWorkingMonitorCaptureSource(); capture_source) {
            hb->set_video_capture_type(capture_source->Name());
        }
        else {
            hb->set_video_capture_type("UnKnown");
        }

        // TODO::
        hb->set_audio_capture_type("WASAPI");
        hb->set_audio_encode_type("OPUS");

        auto& hardware = Hardware::Instance();
        std::stringstream ss;
        if (hardware.gpus_.empty()) {
            ss << "NO GPU";
        }
        else {
            for (const auto &gpu: hardware.gpus_) {
                ss << gpu.name_ << ";";
            }
        }
        auto cpu_name = StringUtil::Trim(hardware.hw_cpu_.name_);
        auto pc_info = std::format("{} / {} / {}", cpu_name, NumFormatter::FormatStorageSize(hardware.memory_size_), ss.str());
        hb->set_pc_info(pc_info);

        // total hardware info
        auto device_info = hb->mutable_device_info();
        // cpu
        {
            auto cpu = device_info->mutable_cpu();
            cpu->set_name(hardware.hw_cpu_.name_);
            cpu->set_id(hardware.hw_cpu_.id_);
            cpu->set_num_cores(hardware.hw_cpu_.num_cores_);
            cpu->set_max_clock_speed(hardware.hw_cpu_.max_clock_speed_);
        }

        // memory
        {
            auto memory = device_info->mutable_memory();
            memory->set_total_size(hardware.memory_size_);
        }

        // gpu
        {
            auto gpus = device_info->mutable_gpus();
            for (const auto& item : hardware.gpus_) {
                auto gpu = gpus->Add();
                gpu->set_name(item.name_);
                gpu->set_size_in_bytes(item.size_);
                gpu->set_size_str(item.size_str_);
                gpu->set_driver_version(item.driver_version_);
                gpu->set_pnp_device_id(item.pnp_device_id_);
            }
        }

        // disks
        {
            auto disks = device_info->mutable_disks();
            for (const auto& item : hardware.hw_disks_) {
                auto disk = disks->Add();
                disk->set_name(item.name_);
                disk->set_model(item.model_);
                disk->set_status(item.status_);
                disk->set_serial_number(item.serial_number_);
                disk->set_interface_type(item.interface_type_);
            }
        }

        // desktop name
        hb->set_desktop_name(hardware.desktop_name_);

        // os name
        static std::string os_name;
        if (os_name.empty()) {
            os_name = GetWindowsProductName();
        }
        hb->set_os_name(os_name);

        //
        auto video_capture_source = app->GetWorkingMonitorCaptureSource();
        auto video_encoders = app->GetWorkingVideoEncoders();
        if (video_capture_source && !video_encoders.empty()) {
            auto captures_info = video_capture_source->WorkingCaptures();
            for (const auto& [name, info] : captures_info) {
                auto monitors_info = hb->mutable_monitors_info();
                IsolatedMonitorStatisticsInfoInRender st_info;
                // capture info
                st_info.set_name(info->target_name_);
                st_info.set_capture_fps(info->fps_);
                st_info.set_capture_frame_width(info->capture_frame_width_);
                st_info.set_capture_frame_height(info->capture_frame_height_);

                // encoder info
                if (video_encoders.contains(info->target_name_)) {
                    auto video_encoder = video_encoders[info->target_name_];
                    auto video_encoders_info = video_encoder->WorkingCaptures();
                    if (video_encoders_info.contains(info->target_name_)) {
                        auto encoder_info = video_encoders_info[info->target_name_];
                        st_info.set_encoder_name(encoder_info->encoder_name_);
                        st_info.set_encode_fps(encoder_info->fps_);
                    }
                }
                monitors_info->insert({name, st_info});
            }
        }

        return ProtoAsData(msg);
    }

    std::shared_ptr<Data> NetMessageMaker::MakeVideoFrameMsg(const px::VideoType& vt, const std::shared_ptr<Data>& data,
                                                   uint64_t frame_index, int frame_width, int frame_height, bool key,
                                                   const std::string& display_name, int mon_left,
                                                   int mon_top, int mon_right, int mon_bottom, const px::EImageFormat& img_format, int mon_index) {
        auto msg = std::make_shared<Message>();
        msg->set_type(px::kVideoFrame);
        auto frame = new VideoFrame();
        frame->set_type(vt);
        frame->set_data(data->AsString());
        frame->set_frame_index(frame_index);
        frame->set_key(key);
        frame->set_frame_width(frame_width);
        frame->set_frame_height(frame_height);
        frame->set_mon_name(display_name);
        frame->set_mon_left(mon_left);
        frame->set_mon_top(mon_top);
        frame->set_mon_right(mon_right);
        frame->set_mon_bottom(mon_bottom);
        frame->set_mon_index(mon_index);
        frame->set_image_format(img_format);
        msg->set_allocated_video_frame(frame);
        return ProtoAsData(msg);
    }

    std::shared_ptr<Data> NetMessageMaker::MakeAudioFrameMsg(const std::shared_ptr<Data>& data,
                                                   int samples, int channels, int bits, int frame_size) {
        auto msg = std::make_shared<Message>();
        msg->set_type(px::kAudioFrame);
        auto frame = new AudioFrame();
        frame->set_data(data->Bytes().data(), data->Size());
        frame->set_samples(samples);
        frame->set_channels(channels);
        frame->set_bits(bits);
        frame->set_frame_size(frame_size);
        msg->set_allocated_audio_frame(frame);
        return ProtoAsData(msg);
    }

    std::shared_ptr<Data> NetMessageMaker::MakeCursorInfoSyncMsg(uint32_t x, uint32_t y, uint32_t hotspot_x, uint32_t hotspot_y,
                                                       uint32_t  width, uint32_t height, bool visable,
                                                       const std::shared_ptr<Data>& data, uint32_t type) {
        auto msg = std::make_shared<Message>();
        msg->set_type(px::kCursorInfoSync);
        auto cursor_info = new CursorInfoSync();
        cursor_info->set_visible(visable);
        cursor_info->set_width(width);
        cursor_info->set_height(height);
        cursor_info->set_x(x);
        cursor_info->set_y(y);
        cursor_info->set_hotspot_x(hotspot_x);
        cursor_info->set_hotspot_y(hotspot_y);
        if (data) {
            cursor_info->set_bitmap(data->MutableBytes().data(), data->Size());
        }
        cursor_info->set_type((CursorInfoSync::CursorType)type);
        msg->set_allocated_cursor_info_sync(cursor_info);
        return ProtoAsData(msg);
    }

    std::shared_ptr<Data> NetMessageMaker::MakeMonitorSwitched(const std::string& name, const int& mon_index) {
        px::Message msg;
        msg.set_type(kMonitorSwitched);
        msg.mutable_monitor_switched()->set_name(name);
        msg.mutable_monitor_switched()->set_index(mon_index);
        return ProtoAsData(&msg);
    }

    std::shared_ptr<Data> NetMessageMaker::MakeVideoCodecChanged(px::VideoType video_type, bool full_color, const std::string& reason) {
        px::Message msg;
        msg.set_type(kVideoCodecChanged);
        auto* body = msg.mutable_video_codec_changed();
        body->set_video_type(video_type);
        body->set_full_color(full_color);
        body->set_reason(reason);
        return ProtoAsData(&msg);
    }

    std::shared_ptr<Data> NetMessageMaker::MakeGameStatusChanged(px::GameStatusChanged::GameStatus status, const std::string& detail) {
        px::Message msg;
        msg.set_type(kGameStatusChanged);
        auto* body = msg.mutable_game_status_changed();
        body->set_status(status);
        body->set_detail(detail);
        return ProtoAsData(&msg);
    }

    std::shared_ptr<Data> NetMessageMaker::MakeInstanceStopped(const std::string& reason) {
        px::Message msg;
        msg.set_type(kInstanceStopped);
        auto* body = msg.mutable_instance_stopped();
        body->set_reason(reason);
        return ProtoAsData(&msg);
    }

}
