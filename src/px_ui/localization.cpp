#include "px_ui/localization.h"

#include <array>
#include <cstddef>
#include <utility>

namespace px::ui {
namespace {

constexpr std::size_t kTextCount{std::to_underlying(TextId::Count)};
using Catalog = std::array<std::string_view, kTextCount>;

constexpr Catalog kEnglish{
    "Render node console",
    "Remote control",
    "Cloud applications",
    "Server status",
    "Security",
    "Settings",
    "Hardware",
    "Exit programs",
    "Settings / Network",
    "Connection addresses and node service ports",
    "Authorization",
    "Resolved control endpoints",
    "Supervisor",
    "Node management and status",
    "Relay",
    "Reliable routed connection",
    "Node public address",
    "Optional. Leave empty when automatic address detection is suitable.",
    "Public IP or hostname",
    "Node listening ports",
    "Service management port",
    "Local service management",
    "Desktop connection port",
    "Desktop render connection",
    "Application port pool",
    "Dynamically allocated applications",
    "RTC media pool",
    "Browser sessions",
    "Panel listening port",
    "Local panel API",
    "Save",
    "Verify",
    "Authorization information is invalid.",
    "The public address is invalid.",
    "Verifying...",
    "Verification succeeded.",
    "Saving...",
    "Saved.",
    "Operation failed.",
    "Settings are saved. Restart the render service now?",
    "Restart now",
    "Later",
    "UI preview: existing Panel services are not connected yet.",
    "Preview saved locally. Business persistence is intentionally not connected.",
    "English",
    "简体中文",
    "Dark",
    "Light",
    "Controller driver",
    "Render service",
    "Node service",
    "Ready",
    "Unavailable",
    "Install",
    "Restart",
    "Network addresses",
    "Wired",
    "Wireless",
    "Audio format",
    "This page is pending UI migration; the existing product workflow remains unchanged.",
};

constexpr Catalog kSimplifiedChinese{
    "渲染节点控制台",
    "远程控制",
    "云应用",
    "服务状态",
    "安全",
    "设置",
    "硬件",
    "退出程序",
    "设置 / 网络",
    "连接地址与节点服务端口",
    "授权信息",
    "解析后的控制端点",
    "管理服务",
    "节点管理与状态",
    "转发服务",
    "可靠转发连接",
    "节点公网地址",
    "可选。自动地址探测可用时请留空。",
    "公网 IP 或域名",
    "节点监听端口",
    "服务管理端口",
    "本机服务管理",
    "桌面连接端口",
    "桌面渲染连接",
    "应用端口池",
    "动态分配的应用端口",
    "RTC 媒体端口池",
    "浏览器会话",
    "Panel 监听端口",
    "本机 Panel 接口",
    "保存",
    "验证",
    "授权信息无效。",
    "节点公网地址无效。",
    "正在验证……",
    "验证成功。",
    "正在保存……",
    "已保存。",
    "操作失败。",
    "设置已保存，是否立即重启渲染服务？",
    "立即重启",
    "稍后",
    "UI 预览：尚未连接现有 Panel 服务。",
    "预览设置已保存在本地，尚未接入业务持久化。",
    "English",
    "简体中文",
    "深色",
    "浅色",
    "控制器驱动",
    "渲染服务",
    "节点服务",
    "正常",
    "不可用",
    "安装",
    "重启",
    "网络地址",
    "有线",
    "无线",
    "音频格式",
    "该页面正在迁移 UI，现有产品业务流程保持不变。",
};

static_assert(kEnglish.size() == kTextCount);
static_assert(kSimplifiedChinese.size() == kTextCount);

} // namespace

Localizer::Localizer(const Language language) noexcept : language_{language} {}

void Localizer::SetLanguage(const Language language) noexcept {
    language_ = language;
}

Language Localizer::CurrentLanguage() const noexcept {
    return language_;
}

std::string_view Localizer::Text(const TextId id) const noexcept {
    const std::size_t index{std::to_underlying(id)};
    if (index >= kTextCount) {
        return {};
    }
    return language_ == Language::SimplifiedChinese ? kSimplifiedChinese[index] : kEnglish[index];
}

bool CatalogsAreComplete() noexcept {
    const auto complete = [](const Catalog& catalog) {
        for (const std::string_view text : catalog) {
            if (text.empty()) {
                return false;
            }
        }
        return true;
    };
    return complete(kEnglish) && complete(kSimplifiedChinese);
}

} // namespace px::ui
