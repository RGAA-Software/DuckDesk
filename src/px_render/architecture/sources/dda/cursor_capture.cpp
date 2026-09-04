//
// Created by RGAA on 2023/8/20.
//
#include "cursor_capture.h"
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>
#include "px_common_new/message_notifier.h"
#include "px_common_new/data.h"
#include "px_common_new/time_util.h"
#include "px_capture_new/capture_message.h"
#include "px_common_new/log.h"
#include "px_message.pb.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "dda_capture_source.h"

namespace px
{
    namespace {

    struct BitmapBytes final {
        BITMAP description{};
        std::vector<std::uint8_t> bytes;
    };

    struct CursorPixels final {
        std::uint32_t width{0};
        std::uint32_t height{0};
        std::vector<std::uint8_t> bytes;
    };

    class IconInfoGuard final {
    public:
        explicit IconInfoGuard(ICONINFO info) noexcept : info_(info) {}
        ~IconInfoGuard() {
            if (info_.hbmColor) {
                DeleteObject(info_.hbmColor);
            }
            if (info_.hbmMask) {
                DeleteObject(info_.hbmMask);
            }
        }

        IconInfoGuard(const IconInfoGuard&) = delete;
        IconInfoGuard& operator=(const IconInfoGuard&) = delete;
        [[nodiscard]] const ICONINFO& Get() const noexcept { return info_; }

    private:
        ICONINFO info_{};
    };

    class CopiedIcon final {
    public:
        explicit CopiedIcon(HICON icon) noexcept : icon_(icon) {}
        ~CopiedIcon() {
            if (icon_) {
                DestroyIcon(icon_);
            }
        }

        CopiedIcon(const CopiedIcon&) = delete;
        CopiedIcon& operator=(const CopiedIcon&) = delete;
        [[nodiscard]] HICON Get() const noexcept { return icon_; }
        [[nodiscard]] explicit operator bool() const noexcept { return icon_ != nullptr; }

    private:
        HICON icon_{nullptr};
    };

    [[nodiscard]] std::optional<BitmapBytes> ReadBitmap(
        HBITMAP bitmap_handle) { // NOLINT(gammaray-raw-pointer-boundary): borrowed Win32 handle
        BitmapBytes result;
        if (GetObject(bitmap_handle, sizeof(result.description), &result.description) == 0) {
            return std::nullopt;
        }
        const auto byte_count = static_cast<std::size_t>(result.description.bmWidthBytes) *
            static_cast<std::size_t>(std::abs(result.description.bmHeight));
        result.bytes.resize(byte_count);
        if (GetBitmapBits(bitmap_handle, static_cast<LONG>(byte_count), result.bytes.data()) == 0) {
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] std::uint8_t BitToAlpha(
        const std::vector<std::uint8_t>& data,
        const std::size_t pixel,
        const bool invert,
        const std::size_t bit_offset = 0) {
        const auto bit = bit_offset + pixel;
        const bool alpha = ((data.at(bit / 8) >> (7 - bit % 8)) & 1) != 0;
        return invert ? (alpha ? 0xFF : 0) : (alpha ? 0 : 0xFF);
    }

    [[nodiscard]] bool BitmapHasAlpha(
        const std::vector<std::uint8_t>& data,
        const std::size_t pixel_count) {
        for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
            if (data.at(pixel * 4 + 3) != 0) {
                return true;
            }
        }
        return false;
    }

    void ApplyMask(
        std::vector<std::uint8_t>& color,
        const BitmapBytes& mask) {
        const auto height = std::abs(mask.description.bmHeight);
        for (LONG y = 0; y < height; ++y) {
            for (LONG x = 0; x < mask.description.bmWidth; ++x) {
                const auto mask_bit = static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(mask.description.bmWidthBytes * 8) +
                    static_cast<std::size_t>(x);
                const auto color_offset =
                    (static_cast<std::size_t>(y) * mask.description.bmWidth + x) * 4 + 3;
                if (color_offset < color.size()) {
                    color[color_offset] = BitToAlpha(mask.bytes, mask_bit, false);
                }
            }
        }
    }

    [[nodiscard]] std::optional<CursorPixels> CopyFromColor(const ICONINFO& info) {
        auto color = ReadBitmap(info.hbmColor);
        if (!color || color->description.bmBitsPixel < 32) {
            return std::nullopt;
        }
        if (const auto mask = ReadBitmap(info.hbmMask)) {
            const auto pixels = static_cast<std::size_t>(color->description.bmWidth) *
                static_cast<std::size_t>(std::abs(color->description.bmHeight));
            if (!BitmapHasAlpha(color->bytes, pixels)) {
                ApplyMask(color->bytes, *mask);
            }
        }
        return CursorPixels{
            .width = static_cast<std::uint32_t>(color->description.bmWidth),
            .height = static_cast<std::uint32_t>(std::abs(color->description.bmHeight)),
            .bytes = std::move(color->bytes),
        };
    }

    [[nodiscard]] std::optional<CursorPixels> CopyFromMask(const ICONINFO& info) {
        auto mask = ReadBitmap(info.hbmMask);
        if (!mask) {
            return std::nullopt;
        }
        const auto height = std::abs(mask->description.bmHeight) / 2;
        const auto width = mask->description.bmWidth;
        const auto pixels = static_cast<std::size_t>(height) * width;
        const auto color_half_bit_offset = static_cast<std::size_t>(
            mask->description.bmWidthBytes * height) * 8;
        CursorPixels output{
            .width = static_cast<std::uint32_t>(width),
            .height = static_cast<std::uint32_t>(height),
            .bytes = std::vector<std::uint8_t>(pixels * 4, 0),
        };
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            const auto alpha = BitToAlpha(mask->bytes, pixel, false);
            const auto color = BitToAlpha(
                mask->bytes, pixel, true, color_half_bit_offset);
            const auto offset = pixel * 4;
            if (!alpha) {
                output.bytes[offset + 3] = color;
            } else {
                const auto rgb = color != 0 ? 0xFF : 0x00;
                output.bytes[offset] = rgb;
                output.bytes[offset + 1] = rgb;
                output.bytes[offset + 2] = rgb;
                output.bytes[offset + 3] = 0xFF;
            }
        }
        return output;
    }

    [[nodiscard]] std::optional<CursorPixels> CaptureIconPixels(const ICONINFO& info) {
        if (auto color = CopyFromColor(info)) {
            return color;
        }
        return CopyFromMask(info);
    }

    void ReorderRgba(CursorPixels& cursor) {
        for (std::size_t offset = 0; offset + 2 < cursor.bytes.size(); offset += 4) {
            std::swap(cursor.bytes[offset], cursor.bytes[offset + 2]);
        }
    }

    }  // namespace

    CursorCapture::CursorCapture(
        const std::shared_ptr<DdaCaptureSource>& owner)
        : owner_(owner) {
        last_cursor_bitmap_data_ = Data::Make(nullptr, 1);
    }

    bool CursorCapture::CaptureCursorIcon(CaptureCursorBitmap& data, HICON icon) {
        if (!icon) {
            return false;
        }
        ICONINFO icon_info{};
        if (!GetIconInfo(icon, &icon_info)) {
            return false;
        }
        const IconInfoGuard icon_info_guard(icon_info);
        auto pixels = CaptureIconPixels(icon_info_guard.Get());
        if (!pixels) {
            return false;
        }
        ReorderRgba(*pixels);
        data.data_ = Data::From(std::string(pixels->bytes.begin(), pixels->bytes.end()));
        data.width_ = pixels->width;
        data.height_ = pixels->height;
        data.hotspot_x_ = icon_info.xHotspot;
        data.hotspot_y_ = icon_info.yHotspot;
        return true;
    }

    void CursorCapture::Capture() {
        CaptureCursorBitmap cursor_bitmap;
        CURSORINFO ci = {0};
        ci.cbSize = sizeof(ci);

        if (!GetCursorInfo(&ci)) {
            cursor_bitmap.visible_ = true;
            return;
        }
        cursor_bitmap.visible_ = (ci.flags & CURSOR_SHOWING) == CURSOR_SHOWING;
        cursor_bitmap.x_ = ci.ptScreenPos.x;
        cursor_bitmap.y_ = ci.ptScreenPos.y;

        static HCURSOR cursor_arrow = LoadCursorW(nullptr, IDC_ARROW);
        static HCURSOR cursor_ibeam = LoadCursorW(nullptr, IDC_IBEAM);
        static HCURSOR cursor_wait = LoadCursorW(nullptr, IDC_WAIT);
        static HCURSOR cursor_cross = LoadCursorW(nullptr, IDC_CROSS);
        static HCURSOR cursor_uparrow = LoadCursorW(nullptr, IDC_UPARROW);
        static HCURSOR cursor_size = LoadCursorW(nullptr, IDC_SIZE);
        static HCURSOR cursor_icon = LoadCursorW(nullptr, IDC_ICON);
        static HCURSOR cursor_sizenwse = LoadCursorW(nullptr, IDC_SIZENWSE);
        static HCURSOR cursor_sizenesw = LoadCursorW(nullptr, IDC_SIZENESW);
        static HCURSOR cursor_sizewe = LoadCursorW(nullptr, IDC_SIZEWE);
        static HCURSOR cursor_sizens = LoadCursorW(nullptr, IDC_SIZENS);
        static HCURSOR cursor_sizeall = LoadCursorW(nullptr, IDC_SIZEALL);
        static HCURSOR cursor_hand = LoadCursorW(nullptr, IDC_HAND);
        static HCURSOR cursor_help = LoadCursorW(nullptr, IDC_HELP);
        static HCURSOR cursor_pin = LoadCursorW(nullptr, IDC_PIN);
        static HCURSOR cursor_person = LoadCursorW(nullptr, IDC_PERSON);

        if (ci.hCursor == cursor_arrow) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcArrow;
        } else if (ci.hCursor == cursor_ibeam) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcIBeam;
        } else if (ci.hCursor == cursor_wait) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcWait;
        } else if (ci.hCursor == cursor_cross) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcCross;
        } else if (ci.hCursor == cursor_uparrow) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcUpArrow;
        } else if (ci.hCursor == cursor_size) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcSize;
        } else if (ci.hCursor == cursor_icon) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcIcon;
        } else if (ci.hCursor == cursor_sizenwse) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcSizeNWSE;
        } else if (ci.hCursor == cursor_sizenesw) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcSizeNESW;
        } else if (ci.hCursor == cursor_sizewe) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcSizeWE;
        } else if (ci.hCursor == cursor_sizens) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcSizeNS;
        } else if (ci.hCursor == cursor_sizeall) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcSizeAll;
        } else if (ci.hCursor == cursor_hand) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcHand;
        } else if (ci.hCursor == cursor_help) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcHelp;
        } else if (ci.hCursor == cursor_pin) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcPin;
        } else if (ci.hCursor == cursor_person) {
            cursor_bitmap.type_ = CursorInfoSync::kIdcPerson;
        }

        // RGB Data
        const CopiedIcon icon(CopyIcon(ci.hCursor));
        if (!icon || !CaptureCursorIcon(cursor_bitmap, icon.Get())) {
            return;
        }
        std::string current_data;
        std::string last_data = last_cursor_bitmap_data_->AsString();
        if (cursor_bitmap.data_) {
            current_data = cursor_bitmap.data_->AsString();
        }

        auto event = std::make_shared<PxPluginCursorEvent>();
        event->cursor_info_ = cursor_bitmap;

        if (current_data != last_data && !current_data.empty()) {
            last_cursor_bitmap_data_ = cursor_bitmap.data_;
        }
        else {
            //如果采集到的鼠标图标和上次采集的鼠标图标相同,在一定时间内，就不发送鼠标图标
            auto current_time = TimeUtil::GetCurrentTimestamp();
            if (last_timestamp_ + 2000 > current_time) {
                event->cursor_info_.data_ = nullptr;
                //LOGI("event->cursor_info_.data_ = nullptr;");
            }
        }

        if (event->cursor_info_.data_) {
            last_timestamp_ = TimeUtil::GetCurrentTimestamp();
        }

        if (const auto owner = owner_.lock()) {
            owner->EmitCompatibilityEvent(event);
        }
    }
}
