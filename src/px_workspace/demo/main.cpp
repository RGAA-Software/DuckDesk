#include <windows.h>
#include <gdiplus.h>
#include <UIlib.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <string>

namespace {
constexpr UINT kExercise{WM_APP + 1};

std::wstring ScaleLayout(const std::wstring& xml, UINT dpi) {
    const std::wregex attributes{LR"xml((height|width|inset|childpadding|size)="([0-9,]+)")xml"};
    std::wstring output{};
    std::size_t offset{};
    for (auto match{std::wsregex_iterator{xml.begin(), xml.end(), attributes}}; match != std::wsregex_iterator{}; ++match) {
        output.append(xml, offset, static_cast<std::size_t>(match->position()) - offset);
        output += (*match)[1].str() + L"=\"";
        const std::wstring values{(*match)[2].str()};
        std::size_t start{};
        while (start < values.size()) {
            const auto end{values.find(L',', start)};
            output += std::to_wstring(MulDiv(std::stoi(values.substr(start, end - start)), static_cast<int>(dpi), 96));
            if (end == std::wstring::npos) {
                break;
            }
            output += L',';
            start = end + 1;
        }
        output += L'"';
        offset = static_cast<std::size_t>(match->position() + match->length());
    }
    output.append(xml, offset, std::wstring::npos);
    return output;
}

class DemoWindow final : public DuiLib::CWindowWnd, public DuiLib::INotifyUI {
  public:
    DemoWindow(bool self_test, std::wstring layout, UINT override_dpi)
        : self_test_(self_test), layout_(std::move(layout)), override_dpi_(override_dpi) {}
    ~DemoWindow() override {
        if (registered_) {
            manager_.RemoveNotifier(this); // Borrowed third-party notifier; unregister before manager destruction.
        }
        if (IsWindow(GetHWND())) {
            DestroyWindow(GetHWND());
        }
    }
    LPCTSTR GetWindowClassName() const override { // NOLINT(gammaray-raw-pointer-boundary) DuiLib ABI, static literal only.
        return L"GammaRayWorkspaceUiDemo";
    }
    void OnFinalMessage(HWND) override {
        PostQuitMessage(0);
    }
    bool Ready() const {
        return ready_;
    }
    bool Exercised() const {
        return clicks_ == 3;
    }

    void Notify(DuiLib::TNotifyUI& event) override {
        if (event.sType != L"click") {
            return;
        }
        const std::wstring name{event.pSender->GetName()}; // Sender is borrowed for this synchronous DuiLib callback only.
        if (name == L"close") {
            PostMessage(WM_CLOSE);
            return;
        }
        if (name == L"app" || name == L"browser" || name == L"preview") {
            ++clicks_;
            const std::wstring text{L"已切换演示内容：" + name + L"（未启动外部程序）"};
            manager_.FindControl(L"status")->SetText(text.c_str());
        }
    }

    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override {
        if (message == WM_CREATE) {
            manager_.Init(GetHWND());
            if (!LoadLayout()) {
                return -1;
            }
            for (const std::wstring name : {L"status", L"scale", L"app", L"browser", L"preview", L"close"}) {
                if (!manager_.FindControl(name.c_str())) {
                    return -1;
                }
            }
            registered_ = manager_.AddNotifier(this); // DuiLib borrows notifier until explicit destructor unregistration.
            ready_ = registered_;
            UpdateScale();
            if (self_test_) {
                PostMessage(kExercise);
            }
            return 0;
        }
        if (message == kExercise) {
            manager_.FindControl(L"app")->Activate();
            manager_.FindControl(L"browser")->Activate();
            manager_.FindControl(L"preview")->Activate();
            manager_.FindControl(L"close")->Activate();
            return 0;
        }
        if (message == WM_DPICHANGED) {
            SetWindowPos(GetHWND(), nullptr, reinterpret_cast<const RECT*>(lparam)->left, reinterpret_cast<const RECT*>(lparam)->top,
                         reinterpret_cast<const RECT*>(lparam)->right - reinterpret_cast<const RECT*>(lparam)->left,
                         reinterpret_cast<const RECT*>(lparam)->bottom - reinterpret_cast<const RECT*>(lparam)->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (!LoadLayout()) {
                PostMessage(WM_CLOSE);
            }
            return 0;
        }
        LRESULT result{};
        if (manager_.MessageHandler(message, wparam, lparam, result)) {
            return result;
        }
        return CWindowWnd::HandleMessage(message, wparam, lparam);
    }

  private:
    bool LoadLayout() {
        const UINT dpi{override_dpi_ == 0 ? GetDpiForWindow(GetHWND()) : override_dpi_};
        const std::wstring scaled{ScaleLayout(layout_, dpi)};
        DuiLib::CDialogBuilder builder{};
        // DuiLib owns the control tree after AttachDialog; only transient third-party boundary pointers are used.
        if (!manager_.AttachDialog(builder.Create(scaled.c_str(), 0, nullptr, &manager_))) {
            return false;
        }
        if (!manager_.FindControl(L"scale")) {
            return false;
        }
        UpdateScale();
        if (manager_.GetDefaultFontInfo()->iSize != MulDiv(16, static_cast<int>(dpi), 96)) {
            return false;
        }
        return true;
    }
    void UpdateScale() {
        const UINT dpi{override_dpi_ == 0 ? GetDpiForWindow(GetHWND()) : override_dpi_};
        const std::wstring text{L"当前窗口 DPI：" + std::to_wstring(dpi) + L" / 缩放 " + std::to_wstring(dpi * 100 / 96) + L"%"};
        manager_.FindControl(L"scale")->SetText(text.c_str());
    }
    DuiLib::CPaintManagerUI manager_{};
    bool self_test_{};
    bool registered_{};
    bool ready_{};
    int clicks_{};
    std::wstring layout_{};
    UINT override_dpi_{};
};
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) { // NOLINT(gammaray-raw-pointer-boundary) Windows entry ABI.
    std::array<wchar_t, 32768> executable{};
    const DWORD length{GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()))};
    if (length == 0 || length >= executable.size()) {
        return 2;
    }
    const std::filesystem::path directory{std::filesystem::path{executable.data()}.parent_path()};
    if (!std::filesystem::exists(directory / L"workspace.xml")) {
        MessageBoxW(nullptr, L"缺少 workspace.xml，请从完整发布目录启动。", L"Workspace demo", MB_OK | MB_ICONERROR);
        return 3;
    }
    DuiLib::CPaintManagerUI::SetInstance(instance);
    DuiLib::CPaintManagerUI::SetResourcePath(directory.c_str());
    std::ifstream resource{directory / L"workspace.xml", std::ios::binary};
    const std::string utf8{std::istreambuf_iterator<char>{resource}, std::istreambuf_iterator<char>{}};
    const int count{MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0)};
    if (count <= 0) {
        return 6;
    }
    std::wstring layout(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), layout.data(), count);
    const bool self_test{std::wstring{command}.find(L"--self-test") != std::wstring::npos};
    UINT override_dpi{};
    if (std::wstring{command}.find(L"--scale=150") != std::wstring::npos) {
        override_dpi = 144;
    } else if (std::wstring{command}.find(L"--scale=200") != std::wstring::npos) {
        override_dpi = 192;
    }
    for (int iteration{}; iteration < (self_test ? 8 : 1); ++iteration) {
        const auto window{std::make_unique<DemoWindow>(self_test, layout, override_dpi)};
        const int dpi{static_cast<int>(override_dpi == 0 ? GetDpiForSystem() : override_dpi)};
        window->Create(nullptr, L"GammaRay Workspace · DuiLib Demo", WS_OVERLAPPEDWINDOW, 0, 60, 60, MulDiv(860, dpi, 96), MulDiv(470, dpi, 96));
        if (!window->Ready()) {
            return 4;
        }
        window->ShowWindow(true);
        DuiLib::CPaintManagerUI::MessageLoop();
        if (self_test && !window->Exercised()) {
            return 5;
        }
    }
    DuiLib::CPaintManagerUI::Term();
    return 0;
}
