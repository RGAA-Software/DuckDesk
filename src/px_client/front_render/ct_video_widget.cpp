#include "ct_video_widget.h"

#include "px_message.pb.h"
#include "ct_qt_key_converter.h"
#include "px_common/log.h"
#include "px_common/data.h"
#include "px_common/time_util.h"
#include "px_common/thread.h"
#include "px_common/time_util.h"
#include "px_client_sdk/thunder_sdk.h"
#include "px_client/ct_client_context.h"
#include "px_client/ct_app_message.h"
#include "px_client/ct_settings.h"
#include "px_common/time_util.h"
#include "px_message/proto_converter.h"
#ifdef WIN32
#include <Windows.h>
#endif
#include <qdebug.h>
#include <string>
#include <atomic>

namespace px
{

    namespace {
        const char* QtMouseButtonName(Qt::MouseButton button) {
            switch (button) {
                case Qt::LeftButton: return "Left";
                case Qt::RightButton: return "Right";
                case Qt::MiddleButton: return "Middle";
                case Qt::BackButton: return "Back";
                case Qt::ForwardButton: return "Forward";
                default: return "Other";
            }
        }

        std::string DescribeButtonFlags(int buttons) {
            std::string s;
            auto append = [&](const char* name) {
                if (!s.empty()) s += "|";
                s += name;
            };
            if (buttons & ButtonFlag::kMouseMove) append("MOVE");
            if (buttons & ButtonFlag::kLeftMouseButtonDown) append("LDOWN");
            if (buttons & ButtonFlag::kLeftMouseButtonUp) append("LUP");
            if (buttons & ButtonFlag::kMiddleMouseButtonDown) append("MDOWN");
            if (buttons & ButtonFlag::kMiddleMouseButtonUp) append("MUP");
            if (buttons & ButtonFlag::kRightMouseButtonDown) append("RDOWN");
            if (buttons & ButtonFlag::kRightMouseButtonUp) append("RUP");
            if (buttons & ButtonFlag::kMouseEventWheel) append("WHEEL");
            if (buttons & ButtonFlag::kMouseEventHWheel) append("HWHEEL");
            if (s.empty()) s = "NONE";
            return s + "(" + std::to_string(buttons) + ")";
        }

        bool IsPureMouseMove(const MouseEventDesc& d) {
            return !d.pressed && !d.released && d.data == 0
                && (d.buttons == 0 || d.buttons == ButtonFlag::kMouseMove);
        }

        // [LAT-input] 客户端输入排队计时统计(evt_cache_thread 排队 + 忙等背压)
        std::atomic<uint64_t> g_input_queued{0};
        std::atomic<uint64_t> g_input_queue_us_sum{0};
        std::atomic<uint64_t> g_input_queue_us_max{0};
        std::atomic<uint64_t> g_input_wait_rounds_sum{0};

        void DumpInputLatencyIfDue() {
            static std::atomic<uint64_t> s_last_dump_us{0};
            auto now = TimeUtil::GetCurrentTimePointUS();
            auto last = s_last_dump_us.load();
            if (now - last < 5000000) {
                return;
            }
            if (!s_last_dump_us.compare_exchange_weak(last, now)) {
                return;
            }
            auto n = g_input_queued.exchange(0);
            auto sum = g_input_queue_us_sum.exchange(0);
            auto mx = g_input_queue_us_max.exchange(0);
            auto rounds = g_input_wait_rounds_sum.exchange(0);
            LOGI("[LAT-input] client queue events={} avg_us={} max_us={} busywait_rounds={}",
                 n, n > 0 ? (sum / n) : 0, mx, rounds);
        }
    }

	VideoWidget::VideoWidget(const std::shared_ptr<ClientContext>& ctx, const std::shared_ptr<ThunderSdk>& sdk, int dup_idx) {
        TimeDuration dr("VideoWidget");
		this->context_ = ctx;
        // to do, dup_idx_ 已经废弃，待删除
        this->dup_idx_ = dup_idx;
        this->key_converter_ = std::make_shared<QtKeyConverter>();
        this->sdk_ = sdk;
        this->settings_ = Settings::Instance();
        this->evt_cache_thread_ = Thread::Make("evt_cache_thread", 256);
        this->evt_cache_thread_->Poll();
	}

	VideoWidget::~VideoWidget() = default;

	void VideoWidget::OnWidgetResize(int w, int h) {
		this->widget_width_ = w;
		this->widget_height_ = h;
	}

	void VideoWidget::OnMouseMoveEvent(QMouseEvent* event, int widget_width, int widget_height) {
        if (widget_width <= 0 || widget_height <= 0) {
            return;
        }
        auto curr_pos = event->pos();
        // 首次只建立基准点
        if (last_cursor_x_ == invalid_position || last_cursor_y_ == invalid_position) {
            last_cursor_x_ = curr_pos.x();
            last_cursor_y_ = curr_pos.y();
            last_cursor_ts_ = TimeUtil::GetCurrentTimestamp();
            return;
        }

        const int dx = curr_pos.x() - last_cursor_x_;
        const int dy = curr_pos.y() - last_cursor_y_;
        if (dx == 0 && dy == 0) {
            return;
        }

        const bool button_held = event->buttons() != Qt::NoButton;
        const auto ts = TimeUtil::GetCurrentTimestamp();
        const auto diff = ts - last_cursor_ts_;
        // client 只上报 ratio;相对位移由 server 根据绝对坐标换算。
        // 空闲移动节流;按住拖动必须连续上报新 ratio(游戏中键转视角)。
        if (!button_held) {
            if ((std::abs(dx) < 2 && std::abs(dy) < 2) || diff < 2) {
                return;
            }
        }

        MouseEventDesc mouse_event_desc;
        mouse_event_desc.buttons = ButtonFlag::kMouseMove;
        mouse_event_desc.x_ratio = ((float)curr_pos.x()) / ((float)widget_width);
        mouse_event_desc.y_ratio = ((float)curr_pos.y()) / ((float)widget_height);
        // 只发 ratio;相对位移由 server 换算(delta_* 保持默认 0)

        last_cursor_ts_ = ts;
        last_cursor_x_ = curr_pos.x();
        last_cursor_y_ = curr_pos.y();
        SendMouseEvent(mouse_event_desc);
	}

	void VideoWidget::OnMousePressEvent(QMouseEvent* event, int widget_width, int widget_height) {
        auto curr_pos = event->pos();
        MouseEventDesc mouse_event_desc;
        mouse_event_desc.buttons = 0;
        auto pressed_button = 0;
        if(event->button() == Qt::LeftButton) {
            pressed_button = ButtonFlag::kLeftMouseButtonDown;
        }
        if(event->button() == Qt::RightButton) {
            pressed_button = ButtonFlag::kRightMouseButtonDown;
        }
        if(event->button() == Qt::MiddleButton) {
            pressed_button = ButtonFlag::kMiddleMouseButtonDown;
        }

        mouse_event_desc.buttons = pressed_button;
        mouse_event_desc.pressed = true;
        mouse_event_desc.x_ratio = ((float)curr_pos.x()) / ((float)(widget_width));
        mouse_event_desc.y_ratio = ((float)curr_pos.y()) / ((float)(widget_height));
        last_cursor_x_ = curr_pos.x();
        last_cursor_y_ = curr_pos.y();
        last_cursor_ts_ = TimeUtil::GetCurrentTimestamp();
        if (pressed_button == 0) {
            LOGW("[InputSend] mouse press ignored, unmapped Qt button={} buttons=0x{:x} pos=({},{}) size={}x{}",
                 QtMouseButtonName(event->button()), static_cast<unsigned>(event->buttons()),
                 curr_pos.x(), curr_pos.y(), widget_width, widget_height);
        } else {
            LOGI("[InputSend] mouse press qt={} -> {} ratio=({:.4f},{:.4f}) pos=({},{}) size={}x{}",
                 QtMouseButtonName(event->button()), DescribeButtonFlags(pressed_button),
                 mouse_event_desc.x_ratio, mouse_event_desc.y_ratio,
                 curr_pos.x(), curr_pos.y(), widget_width, widget_height);
        }
        SendMouseEvent(mouse_event_desc);

        context_->SendAppMessage(MsgClientMousePressed {

        });
	}

	void VideoWidget::OnMouseReleaseEvent(QMouseEvent* event, int widget_width, int widget_height) {
        auto curr_pos = event->pos();
        // 抬起前若位置有变化,先补发 MOVE(仅 ratio),供 server 换算相对位移
        if (widget_width > 0 && widget_height > 0
            && last_cursor_x_ != invalid_position && last_cursor_y_ != invalid_position) {
            if (curr_pos.x() != last_cursor_x_ || curr_pos.y() != last_cursor_y_) {
                MouseEventDesc move_desc;
                move_desc.buttons = ButtonFlag::kMouseMove;
                move_desc.x_ratio = ((float)curr_pos.x()) / ((float)widget_width);
                move_desc.y_ratio = ((float)curr_pos.y()) / ((float)widget_height);
                LOGI("[InputSend] pre-release move ratio=({:.4f},{:.4f})",
                     move_desc.x_ratio, move_desc.y_ratio);
                SendMouseEvent(move_desc);
                last_cursor_x_ = curr_pos.x();
                last_cursor_y_ = curr_pos.y();
                last_cursor_ts_ = TimeUtil::GetCurrentTimestamp();
            }
        }

        MouseEventDesc mouse_event_desc;
        auto released_button = 0;
        if (event->button() == Qt::LeftButton) {
            released_button = ButtonFlag::kLeftMouseButtonUp;
        }
        if (event->button() == Qt::RightButton) {
            released_button = ButtonFlag::kRightMouseButtonUp;
        }
        if (event->button() == Qt::MiddleButton) {
            released_button = ButtonFlag::kMiddleMouseButtonUp;
        }
        mouse_event_desc.buttons = released_button;
        mouse_event_desc.released = true;
        mouse_event_desc.x_ratio = ((float)curr_pos.x()) / ((float)(widget_width));
        mouse_event_desc.y_ratio = ((float)curr_pos.y()) / ((float)(widget_height));
        if (released_button == 0) {
            LOGW("[InputSend] mouse release ignored, unmapped Qt button={} buttons=0x{:x} pos=({},{})",
                 QtMouseButtonName(event->button()), static_cast<unsigned>(event->buttons()),
                 curr_pos.x(), curr_pos.y());
        } else {
            LOGI("[InputSend] mouse release qt={} -> {} ratio=({:.4f},{:.4f}) pos=({},{})",
                 QtMouseButtonName(event->button()), DescribeButtonFlags(released_button),
                 mouse_event_desc.x_ratio, mouse_event_desc.y_ratio,
                 curr_pos.x(), curr_pos.y());
        }
        SendMouseEvent(mouse_event_desc);
	}

	void VideoWidget::OnMouseDoubleClickEvent(QMouseEvent*) {
	}

	void VideoWidget::OnWheelEvent(QWheelEvent* event, int widget_width, int widget_height) {
        MouseEventDesc mouse_event_desc;
        mouse_event_desc.buttons = 0;
        mouse_event_desc.x_ratio = ((float)last_cursor_x_) / ((float)(widget_width));
        mouse_event_desc.y_ratio = ((float)last_cursor_y_) / ((float)(widget_height));
        QPoint angle_delta = event->angleDelta();
        QPoint numDegrees = event->angleDelta() / 8;
        if (!numDegrees.isNull()) {
            mouse_event_desc.buttons = ButtonFlag::kMouseEventWheel;
            if(angle_delta.x() != 0) {
                mouse_event_desc.data = angle_delta.x();
            }
            if(angle_delta.y() != 0) {
                mouse_event_desc.data = angle_delta.y();
            }
            LOGI("[InputSend] mouse wheel data={} angle=({},{}) pixel=({},{}) ratio=({:.4f},{:.4f})",
                 mouse_event_desc.data, angle_delta.x(), angle_delta.y(),
                 event->pixelDelta().x(), event->pixelDelta().y(),
                 mouse_event_desc.x_ratio, mouse_event_desc.y_ratio);
            SendMouseEvent(mouse_event_desc);
        } else {
            LOGW("[InputSend] mouse wheel ignored, empty angleDelta pixel=({},{})",
                 event->pixelDelta().x(), event->pixelDelta().y());
        }
	}

	void VideoWidget::OnKeyPressEvent(QKeyEvent* e) {
#ifdef WIN32
        SendKeyEvent(e->nativeVirtualKey(), true);
        if (!e->text().isEmpty() &&
            !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            SendTextInput(e->text());
        }
#endif
	}

	void VideoWidget::OnKeyReleaseEvent(QKeyEvent* e) {
#ifdef WIN32
        SendKeyEvent(e->nativeVirtualKey(), false);
#endif
	}

    void VideoWidget::RegisterMouseKeyboardEventCallback(const OnMouseKeyboardEventCallback& cbk) {
        event_cbk_ = cbk;
    }

    void VideoWidget::SendCallback(const std::shared_ptr<NetMessage>& msg) {
        if (event_cbk_) {
            event_cbk_(dup_idx_, msg);
        }
    }

    void VideoWidget::SendKeyEvent(quint32 vk, bool down) {
        if (settings_->only_viewing_) {
            LOGW("[InputSend] drop key vk=0x{:x} down={}, only_viewing", vk, down);
            return;
        }
        LOGI("[InputSend] key vk=0x{:x} down={}", vk, down);
        short num_lock_state = -1;
        if (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE || vk == VK_NUMLOCK
            || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT
            || vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT
            || vk == VK_INSERT || vk == VK_DELETE ) {
            num_lock_state = GetKeyState(VK_NUMLOCK);
        }

        short caps_lock_state = -1;
        if (vk >= 0x41 && vk <= 0x5A) {
            caps_lock_state = GetKeyState(VK_CAPITAL);
        }

        std::map<int, bool> sys_key_status = key_converter_->GetSysKeyStatus();
        auto msg = std::make_shared<Message>();
        msg->set_type(px::kKeyEvent);
        msg->set_device_id(settings_->device_id_);
        msg->set_stream_id(settings_->stream_id_);
        auto key_event = new px::KeyEvent();
        key_event->set_down(down);
        key_event->set_key_code(vk);
        key_event->set_num_lock_status(num_lock_state);
        key_event->set_caps_lock_status(caps_lock_state);
        if (num_lock_state != -1) {
            key_event->set_status_check(px::KeyEvent::kCheckNumLock);
        } else if (caps_lock_state != -1) {
            key_event->set_status_check(px::KeyEvent::kCheckCapsLock);
        } else {
            key_event->set_status_check(px::KeyEvent::kDontCareLockKey);
        }
        auto cur_time = GetCurrentTime();
        key_event->set_timestamp(cur_time);
        msg->set_allocated_key_event(key_event);

        // 记录按下状态(仅 UI 线程访问),用于重连后补发 release
        if (down) {
            pressed_keys_.insert(vk);
        }
        else {
            pressed_keys_.erase(vk);
        }

        // 与鼠标事件走同一个 FIFO 队列投递,保证键鼠事件有序
        const auto sdk = sdk_;
        evt_cache_thread_->Post([msg, sdk, vk, down]() {
            if (auto buffer = px::ProtoAsData(msg); buffer && sdk) {
                sdk->PostMediaMessage(buffer);
                LOGI("[InputSend] key posted vk=0x{:x} down={} bytes={}", vk, down, buffer->Size());
            } else {
                LOGE("[InputSend] key encode/post failed vk=0x{:x} down={} sdk={}",
                     vk, down, sdk != nullptr);
            }
        });
    }

    void VideoWidget::SendTextInput(const QString& text) {
        if (settings_->only_viewing_ || text.isEmpty()) {
            return;
        }
        const auto utf8 = text.toUtf8();
        if (utf8.size() > 4096) {
            LOGW("[InputSend] drop text input, payload too large: {}", utf8.size());
            return;
        }
        auto msg = std::make_shared<Message>();
        msg->set_type(px::kTextInput);
        msg->set_device_id(settings_->device_id_);
        msg->set_stream_id(settings_->stream_id_);
        msg->mutable_text_input()->set_text(utf8.constData(), utf8.size());
        const auto sdk = sdk_;
        evt_cache_thread_->Post([msg, sdk]() {
            if (auto buffer = px::ProtoAsData(msg); buffer && sdk) {
                sdk->PostMediaMessage(buffer);
            }
        });
    }

    void VideoWidget::SendMouseEvent(const MouseEventDesc& mouse_event_desc) {
        if (!sdk_ || settings_->only_viewing_) {
            if (!IsPureMouseMove(mouse_event_desc)) {
                LOGW("[InputSend] drop mouse {}, sdk={} only_viewing={}",
                     DescribeButtonFlags(mouse_event_desc.buttons),
                     sdk_ != nullptr, settings_->only_viewing_);
            }
            return;
        }

        // [LAT-roundtrip] 记录最近一次鼠标发送时刻,供解码出帧时计算操作往返延迟
        px::g_last_mouse_send_us = TimeUtil::GetCurrentTimePointUS();

        auto msg = std::make_shared<Message>();
        msg->set_type(px::kMouseEvent);
        msg->set_device_id(settings_->device_id_);
        msg->set_stream_id(settings_->stream_id_);
        auto mouse_event = new px::MouseEvent();
        mouse_event->set_x_ratio(mouse_event_desc.x_ratio);
        mouse_event->set_y_ratio(mouse_event_desc.y_ratio);
        mouse_event->set_button(mouse_event_desc.buttons);
        auto cur_time = GetCurrentTime();
        mouse_event->set_timestamp(cur_time);
        mouse_event->set_monitor_name(cap_mon_info_.mon_name_);
        mouse_event->set_data(mouse_event_desc.data);
        mouse_event->set_delta_x(mouse_event_desc.dx);
        mouse_event->set_delta_y(mouse_event_desc.dy);
        mouse_event->set_pressed(mouse_event_desc.pressed);
        mouse_event->set_released(mouse_event_desc.released);
        msg->set_allocated_mouse_event(mouse_event);

        // 记录按下状态(仅 UI 线程访问),存下对应的 release flag,用于重连后补发 release
        if (mouse_event_desc.pressed) {
            int up_flag = 0;
            if (mouse_event_desc.buttons == ButtonFlag::kLeftMouseButtonDown) { up_flag = ButtonFlag::kLeftMouseButtonUp; }
            else if (mouse_event_desc.buttons == ButtonFlag::kMiddleMouseButtonDown) { up_flag = ButtonFlag::kMiddleMouseButtonUp; }
            else if (mouse_event_desc.buttons == ButtonFlag::kRightMouseButtonDown) { up_flag = ButtonFlag::kRightMouseButtonUp; }
            if (up_flag != 0) {
                pressed_mouse_buttons_.insert(up_flag);
            }
        }
        else if (mouse_event_desc.released) {
            pressed_mouse_buttons_.erase(mouse_event_desc.buttons);
        }

        // 按住拖动时的 MOVE(仅 ratio)也打日志,便于对照 server-rel
        const bool dragging = !pressed_mouse_buttons_.empty()
            && IsPureMouseMove(mouse_event_desc);
        const bool significant = !IsPureMouseMove(mouse_event_desc) || dragging;
        if (significant) {
            LOGI("[InputSend] queue mouse {} pressed={} released={} data={} ratio=({:.4f},{:.4f}) monitor={} drag={}",
                 DescribeButtonFlags(mouse_event_desc.buttons),
                 mouse_event_desc.pressed, mouse_event_desc.released, mouse_event_desc.data,
                 mouse_event_desc.x_ratio, mouse_event_desc.y_ratio,
                 cap_mon_info_.mon_name_, dragging);
        }

        // 捕获按住状态:工作线程回调里 pressed_mouse_buttons_ 可能已变化
        const bool keep_move_when_busy = !pressed_mouse_buttons_.empty()
            || mouse_event_desc.pressed || mouse_event_desc.released || mouse_event_desc.data != 0;

        // [LAT-input] 记录入队时间戳,在工作线程里算排队耗时
        const uint64_t ts_enqueue_us = TimeUtil::GetCurrentTimePointUS();
        const auto sdk = sdk_;
        const auto event_thread = evt_cache_thread_;

        evt_cache_thread_->Post(
            [msg, sdk, event_thread, mouse_event_desc, keep_move_when_busy,
             significant, ts_enqueue_us]() {
            // [LAT-input] 入队 -> 实际发送前的排队耗时(含下方忙等背压)
            const uint64_t queue_us = TimeUtil::GetCurrentTimePointUS() - ts_enqueue_us;
            ++g_input_queued;
            g_input_queue_us_sum += queue_us;
            auto prev_max = g_input_queue_us_max.load();
            while (queue_us > prev_max && !g_input_queue_us_max.compare_exchange_weak(prev_max, queue_us)) {}

            auto queuing_count = sdk->GetQueuingMediaMsgCount();
            int wait_rounds = 0;
            while (queuing_count > 16 && wait_rounds < 50) {
                LOGI("[InputSend] queuing too many mouse event: {}, cache thread tasks: {}",
                     queuing_count, event_thread->TaskSize());
                TimeUtil::DelayBySleep(1);
                queuing_count = sdk->GetQueuingMediaMsgCount();
                ++wait_rounds;
            }
            g_input_wait_rounds_sum += wait_rounds;
            DumpInputLatencyIfDue();
            // 队列持续积压时,仅丢弃空闲纯移动;按住拖动/press/release/滚轮必须发送
            if (queuing_count > 16 && !keep_move_when_busy) {
                LOGW("[InputSend] drop pure mouse move, queuing media messages: {}", queuing_count);
                return;
            }
            if (auto buffer = px::ProtoAsData(msg); buffer && sdk) {
                sdk->PostMediaMessage(buffer);
                if (significant) {
                    LOGI("[InputSend] mouse posted {} bytes={} queue={}",
                         DescribeButtonFlags(mouse_event_desc.buttons), buffer->Size(), queuing_count);
                }
            } else if (significant) {
                LOGE("[InputSend] mouse encode/post failed {} sdk={}",
                     DescribeButtonFlags(mouse_event_desc.buttons), sdk != nullptr);
            }
        });
    }

    void VideoWidget::ReleaseAllPressedInputs() {
        // 重连成功后调用(仅 UI 线程),补发所有跟踪中的 key/mouse release 并清空,避免远端按键卡死、鼠标粘连
        auto pressed_keys = pressed_keys_;
        pressed_keys_.clear();
        for (auto vk : pressed_keys) {
            LOGI("Release tracked key after reconnected, vk: 0x{:x}", vk);
            SendKeyEvent(vk, false);
        }

        auto pressed_buttons = pressed_mouse_buttons_;
        pressed_mouse_buttons_.clear();
        for (auto up_flag : pressed_buttons) {
            LOGI("Release tracked mouse button after reconnected, flag: {}", up_flag);
            MouseEventDesc mouse_event_desc;
            mouse_event_desc.buttons = up_flag;
            mouse_event_desc.released = true;
            if (widget_width_ > 0 && widget_height_ > 0
                && last_cursor_x_ != invalid_position && last_cursor_y_ != invalid_position) {
                mouse_event_desc.x_ratio = ((float)last_cursor_x_) / ((float)(widget_width_));
                mouse_event_desc.y_ratio = ((float)last_cursor_y_) / ((float)(widget_height_));
            }
            SendMouseEvent(mouse_event_desc);
        }
    }

    void VideoWidget::RefreshImage(const std::shared_ptr<RawImage> &image) {
        if (image->Format() == RawImageFormat::kRawImageI420) {
            this->RefreshI420Image(image);
        }
        else if (image->Format() == RawImageFormat::kRawImageI444) {
            this->RefreshI444Image(image);
        }
        else if (image->Format() == RawImageFormat::kRawImageRGB) {
            this->RefreshRGBBuffer(image->Data(), image->img_width, image->img_height, image->img_ch);
        }
    }

    RawImageFormat VideoWidget::GetDisplayImageFormat() {
        return raw_image_format_;
    }

    void VideoWidget::SetDisplayImageFormat(RawImageFormat format) {
        raw_image_format_ = format;
    }

    void VideoWidget::RefreshCapturedMonitorInfo(const SdkCaptureMonitorInfo& mon_info) {
        cap_mon_info_ = mon_info;
    }

    int VideoWidget::GetCapturingMonitorWidth() {
        return cap_mon_info_.frame_width_;
    }

    int VideoWidget::GetCapturingMonitorHeight() {
        return cap_mon_info_.frame_height_;
    }

    SdkCaptureMonitorInfo VideoWidget::GetCaptureMonitorInfo() {
        return cap_mon_info_;
    }

    void VideoWidget::OnTimer1S() {

    }

    QWidget* VideoWidget::AsWidget() {
        return nullptr;
    }

    void VideoWidget::RefreshRGBBuffer(const char* buf, int width, int height, int channel) {
        std::lock_guard<std::mutex> guard(buf_mtx_);
        int size = width * height * channel;
        if (!rgb_buffer_ || (int)rgb_buffer_->Size() != size) {
            rgb_buffer_ = Data::Allocate( size);
        }
        if (tex_width_ != width || tex_height_ != height) {
            need_create_texture_ = true;
        }
        memcpy(rgb_buffer_->MutableBytes().data(), buf, size);
        tex_width_ = width;
        tex_height_ = height;
        tex_channel_ = channel;

        this->OnUpdate();
    }

    void VideoWidget::RefreshI420Image(const std::shared_ptr<RawImage>& image) {
        std::lock_guard<std::mutex> guard(buf_mtx_);
        int y_buf_size = image->img_width * image->img_height;
        int uv_buf_size = y_buf_size / 4;
        char* buf = image->Data();
        RefreshI420Buffer(buf, y_buf_size,
                          buf + y_buf_size, uv_buf_size,
                          buf + y_buf_size + uv_buf_size, uv_buf_size,
                          image->img_width, image->img_height
        );
    }

    void VideoWidget::RefreshI420Buffer(const char* y_buf, int y_buf_size,
                                              const char* u_buf, int u_buf_size,
                                              const char* v_buf, int v_buf_size,
                                              int width, int height) {
        auto target_y_size = width * height;
        auto target_u_size = width/2 * height/2;
        if (!y_buffer_ || y_buffer_->Size() != y_buf_size) {
            y_buffer_ = Data::Copy(std::span<const char>{y_buf, static_cast<std::size_t>(y_buf_size)});
            need_create_texture_ = true;
        }
        if (!u_buffer_ || u_buffer_->Size() != u_buf_size) {
            u_buffer_ = Data::Copy(std::span<const char>{u_buf, static_cast<std::size_t>(u_buf_size)});
            need_create_texture_ = true;
        }
        if (!v_buffer_ || v_buffer_->Size() != v_buf_size) {
            v_buffer_ = Data::Copy(std::span<const char>{v_buf, static_cast<std::size_t>(v_buf_size)});
            need_create_texture_ = true;
        }

        if (tex_width_ != width || tex_height_ != height) {
            need_create_texture_ = true;
        }
        memcpy(y_buffer_->MutableBytes().data(), y_buf, y_buf_size);
        memcpy(u_buffer_->MutableBytes().data(), u_buf, u_buf_size);
        memcpy(v_buffer_->MutableBytes().data(), v_buf, v_buf_size);

        tex_width_ = width;
        tex_height_ = height;

        this->OnUpdate();
    }

    void VideoWidget::RefreshI444Image(const std::shared_ptr<RawImage>& image) {
        std::lock_guard<std::mutex> guard(buf_mtx_);
        int y_buf_size = image->img_width * image->img_height;
        int uv_buf_size = y_buf_size;
        char* buf = image->Data();
#if 0   // debug: save yuv file
        {
			std::string img_data;
			img_data.resize(image->Size());
			memcpy(img_data.data(), buf, image->Size());
			static int index = 0;
			auto yuv444_file = File::OpenForWrite("RefreshI444Image_" + std::to_string(index % 10) + ".yuv444");
			if (yuv444_file) {
				yuv444_file->Write(0, img_data);
			}
			++index;
		}
#endif
        RefreshI444Buffer(
                buf, y_buf_size,
                buf + y_buf_size, uv_buf_size,
                buf + y_buf_size + uv_buf_size, uv_buf_size,
                image->img_width, image->img_height
        );
    }

    void VideoWidget::RefreshI444Buffer(const char* y_buf, int y_buf_size,
                                              const char* u_buf, int u_buf_size,
                                              const char* v_buf, int v_buf_size,
                                              int width, int height) {
        auto target_y_size = width * height;
        auto target_u_size = width * height;

        if (!y_buffer_ || y_buffer_->Size() != y_buf_size) {
            y_buffer_ = Data::Copy(std::span<const char>{y_buf, static_cast<std::size_t>(y_buf_size)});
            need_create_texture_ = true;
        }
        if (!u_buffer_ || u_buffer_->Size() != u_buf_size) {
            u_buffer_ = Data::Copy(std::span<const char>{u_buf, static_cast<std::size_t>(u_buf_size)});
            need_create_texture_ = true;
        }
        if (!v_buffer_ || v_buffer_->Size() != v_buf_size) {
            v_buffer_ = Data::Copy(std::span<const char>{v_buf, static_cast<std::size_t>(v_buf_size)});
            need_create_texture_ = true;
        }

        if (tex_width_ != width || tex_height_ != height) {
            need_create_texture_ = true;
        }
        memcpy(y_buffer_->MutableBytes().data(), y_buf, y_buf_size);
        memcpy(u_buffer_->MutableBytes().data(), u_buf, u_buf_size);
        memcpy(v_buffer_->MutableBytes().data(), v_buf, v_buf_size);

        tex_width_ = width;
        tex_height_ = height;

        this->OnUpdate();
    }
    
}
