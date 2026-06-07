# tc_common_new 去 Qt 化改造方案

> **目标**：移除 `tc_common_new` 对 Qt6（Core / Core5Compat / Widgets / Network）的全部依赖。
> **前提**：`process_util.cpp`（QProcess 异步机制）和 `image_generator.cpp`（QPainter 绘图）暂时不改造，保留 Qt 依赖。其余文件全部使用 C++20 标准库 + Win32 API 替代。

---

## 一、CMakeLists.txt 修改

### 1.1 删除 Qt 查找与链接

```cmake
# 删除以下行
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Core5Compat Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Core5Compat Widgets Network)

# 删除链接
target_link_libraries(tc_common_new Qt6::Core Qt6::Core5Compat Qt6::Widgets Qt6::Network)
```

### 1.2 补充必要的 Win32 库

去 Qt 后需显式链接以下系统库：

```cmake
if (WIN32)
    target_link_libraries(tc_common_new
        DXGI D3D11 OpenSSL::SSL OpenSSL::Crypto
        Shlwapi.lib Wtsapi32.lib
        iphlpapi.lib        # GetAdaptersAddresses
        shell32.lib         # SHGetKnownFolderPath
        advapi32.lib        # RegOpenKeyExW
        gdiplus.lib         # qr_generator 若改为 GDI+
        dwmapi.lib          # qwidget_helper 保留
    )
endif()
```

### 1.3 C++ 标准提升至 20

已满足（`set(CMAKE_CXX_STANDARD 20)`），改造中可充分利用 `std::format`、`std::ranges` 等特性。

---

## 二、逐文件改造详情

### 2.1 `auto_start.h` / `auto_start.cpp`

**现状**：接口使用 `QString`，内部用 `QSettings` 读写注册表、`QFileInfo` 取程序名、`QDir::toNativeSeparators` 转路径。

**改造方案**：

| 原 Qt 类/函数 | C++20 / Win32 替代 | 说明 |
|---|---|---|
| `QString` | `std::wstring` | 注册表操作全程宽字符 |
| `QSettings` | `RegOpenKeyExW` / `RegSetValueExW` / `RegDeleteValueW` | `advapi32.lib` |
| `QFileInfo::baseName()` | `std::filesystem::path::stem()` | C++17 已支持 |
| `QDir::toNativeSeparators()` | `std::replace(path.begin(), path.end(), L'/', L'\\')` | 或 `PathCanonicalizeW` |
| `QString::fromStdWString` / `toStdString` | `StringUtil::ToUTF8` / `ToWString` | 项目已有工具函数 |

**头文件接口变更**：

```cpp
// auto_start.h
// 修改前
static void SetAutoStart(const QString& exe_path, bool enabled);

// 修改后
static void SetAutoStart(const std::wstring& exe_path, bool enabled);
```

**C++20 可用特性**：
- `std::format(L"{}", exe_path)` 替代字符串拼接（日志或调试输出）。

---

### 2.2 `file.h` / `file.cpp`

**现状**：Windows 分支底层使用 `QFile` + `QFileInfo`，Linux 分支已使用 `fopen`。`file.h` 中 `#ifdef WIN32` 包含了 `QFile`、`QFileInfo`、`QDir`。

**改造方案**：

Windows 分支完全统一为 `std::fstream` 或 `FILE*`，彻底抹除 Qt。

| 原 Qt 类/函数 | C++20 / STL 替代 | 说明 |
|---|---|---|
| `QFile` | `std::fstream`（二进制模式） | `std::ios::binary \| std::ios::in/out` |
| `QFileInfo` | `std::filesystem::path` + `std::filesystem::exists / file_size` | 已包含在代码中 |
| `QIODeviceBase::OpenMode` | 自定义枚举 `enum class OpenMode { ReadOnly, WriteOnly, ... }` | 仅内部使用 |
| `qint64` | `int64_t` | 标准类型替换 |

**关键代码变更示例**：

```cpp
// file.h 删除以下内容
#ifdef WIN32
#include <QFile>
#include <QFileInfo>
#include <QDir>
#endif

// file.h 类成员修改
#ifdef WIN32
    // 删除 std::shared_ptr<QFile> file_;
    // 删除 QFileInfo file_info_;
    std::fstream fs_;   // 新增
#endif
```

```cpp
// file.cpp 中 ToQtOpenMode 改为 ToStdOpenMode
namespace {
    std::ios::openmode ToStdOpenMode(const std::string& mode) {
        std::ios::openmode flags = std::ios::binary;
        if (mode.find('r') != std::string::npos) flags |= std::ios::in;
        if (mode.find('w') != std::string::npos) flags |= std::ios::out | std::ios::trunc;
        if (mode.find('a') != std::string::npos) flags |= std::ios::app;
        if (mode.find('+') != std::string::npos) flags |= std::ios::in | std::ios::out;
        return flags;
    }
}

File::File(const std::string& path, const std::string& mode) {
    // ...
#ifdef WIN32
    fs_.open(path, ToStdOpenMode(mode));
    if (!fs_.is_open()) {
        LOGE("Open file failed, mode: {}, file: {}", mode, path);
        return;
    }
#else
    fp_ = fopen(path.c_str(), mode.c_str());
#endif
}
```

**注意**：`std::fstream` 默认不支持 `uint64_t` offset 的 `seekg`，需用 `fs_.seekg(static_cast<std::streamoff>(offset))`。若需支持超大文件偏移，建议 Windows 下也统一用 `_wfopen` + `fseek`/`ftell`，或 Windows API `CreateFileW` + `SetFilePointerEx`。

**推荐做法**：Windows 分支直接用 `_wfopen_s` / `_fsopen`，保持与 Linux `fopen` 语义一致，减少跨平台差异。

---

### 2.3 `folder_util.h` / `folder_util.cpp`

**现状**：头文件包含大量 Qt 头文件（`QFile`, `QDir`, `QFileInfo`, `QDirIterator`, `QDesktopServices`, `QUrl`）。实现中 `VisitAllByQt`、`CreateDir`、`OpenDir`、`GetProgramDataPath`、`DeleteDir` 均使用 Qt API。

**改造方案**：

| 原 Qt 类/函数 | C++20 / Win32 替代 | 说明 |
|---|---|---|
| `QDirIterator` | `std::filesystem::recursive_directory_iterator` | 已有 `VisitRecursiveFiles` 使用，可直接复用 |
| `QDir::exists / mkpath` | `std::filesystem::exists / create_directories` | 跨平台统一 |
| `QDir::removeRecursively` | `std::filesystem::remove_all` | 注意 `std::error_code` 处理 |
| `QDesktopServices::openUrl` | `ShellExecuteW(NULL, L"open", path.c_str(), ...)` | `shell32.lib` |
| `QStandardPaths::PublicShareLocation` | `SHGetKnownFolderPath(FOLDERID_Public, ...)` | `shell32.lib` |
| `QCoreApplication::applicationPid()` | `GetCurrentProcessId()` | 已在 process_util.cpp 使用，此处不涉及 |

**关键代码变更**：

```cpp
// folder_util.h 删除所有 Qt 头文件 include
#ifdef WIN32
// #include <QFile>       // 删除
// #include <QDir>        // 删除
// #include <QFileInfo>   // 删除
// #include <QDirIterator>// 删除
// #include <QDesktopServices> // 删除
// #include <QUrl>        // 删除
#include <Shlwapi.h>
#endif
```

```cpp
// CreateDir
void FolderUtil::CreateDir(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        LOGE("Create folder failed: {}, ec: {}", path, ec.message());
    }
}

// OpenDir
void FolderUtil::OpenDir(const std::string& path) {
    auto wpath = StringUtil::ToWString(path);
    ShellExecuteW(nullptr, L"open", L"explorer.exe", wpath.c_str(), nullptr, SW_SHOWNORMAL);
}

// GetProgramDataPath
std::wstring FolderUtil::GetProgramDataPath(const std::string& app) {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Public, 0, nullptr, &path))) {
        std::wstring sharedPath(path);
        CoTaskMemFree(path);
        auto app_path = sharedPath + L"\\" + StringUtil::ToWString(app);
        std::error_code ec;
        std::filesystem::create_directories(app_path, ec);
        if (ec) {
            LOGE("Create folder failed: {}", StringUtil::ToUTF8(app_path));
        }
        return app_path;
    }
    return L"";
}

// DeleteDir
bool FolderUtil::DeleteDir(const std::wstring& path) {
    std::error_code ec;
    auto removed = std::filesystem::remove_all(path, ec);
    if (ec) {
        LOGE("DeleteDir failed: {}, ec: {}", StringUtil::ToUTF8(path), ec.message());
        return false;
    }
    return true;
}
```

**C++20 可用特性**：
- `std::format` 用于日志拼接（若 `LOGE` 支持 `std::format` 风格）。

---

### 2.4 `file_util.cpp`

**现状**：`CopyFileExt` 使用 `QFile::exists / remove / copy`；`SelectFileInExplorer` 使用 `QString` + `QProcess::startDetached`；`ReName` 使用 `QFile::exists / rename`。

**改造方案**：

| 原 Qt 类/函数 | C++20 / Win32 替代 | 说明 |
|---|---|---|
| `QFile::exists / remove / copy / rename` | `std::filesystem::exists / remove / copy_file / rename` | 全平台统一 |
| `QProcess::startDetached` | `ShellExecuteW` / `CreateProcessW` | explorer 高亮命令行：`explorer.exe /select,"path"` |
| `QString::fromStdString` / `arg` | `std::wstring` + `std::format` | C++20 `std::format(L"explorer.exe /select,\"{}\"", path)` |

**关键代码变更**：

```cpp
bool FileUtil::CopyFileExt(const std::string& from, const std::string& to, bool force_replace) {
    std::error_code ec;
    if (std::filesystem::exists(to, ec)) {
        if (force_replace) {
            std::filesystem::remove(to, ec);
        } else {
            return true;
        }
    }
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

void FileUtil::SelectFileInExplorer(const std::string& p) {
    auto wpath = StringUtil::ToWString(p);
    std::replace(wpath.begin(), wpath.end(), L'/', L'\\');
    auto cmd = std::format(LR"(explorer.exe /select,"{}")", wpath);
    // 使用 ShellExecuteW 或 _wsystem
    _wsystem(cmd.c_str());
}

bool FileUtil::ReName(const std::string& old_path, const std::string& new_path) {
    std::error_code ec;
    if (!std::filesystem::exists(old_path, ec)) {
        return false;
    }
    std::filesystem::rename(old_path, new_path, ec);
    return !ec;
}
```

---

### 2.5 `string_util.cpp`

**现状**：`Trim()` 函数在 Windows 下使用 `QString::trimmed()`。

**改造方案**：

```cpp
std::string StringUtil::Trim(const std::string& str) {
    auto view = std::string_view(str);
    auto start = view.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    auto end = view.find_last_not_of(" \t\r\n");
    return std::string(view.substr(start, end - start + 1));
}
```

> 注：`std::string_view` 是 C++17 特性，但 C++20 中配合 `std::ranges` 可进一步简化。此处直接手写最清晰。

---

### 2.6 `hardware.cpp` / `ip_util.cpp`

**现状**：均使用 `QNetworkInterface::allInterfaces()` + `QList<QNetworkAddressEntry>` 枚举网卡、获取 MAC / IP / 广播地址。

**改造方案**：统一使用 Win32 `GetAdaptersAddresses` 替代。

| 原 Qt 类/函数 | C++20 / Win32 替代 | 说明 |
|---|---|---|
| `QNetworkInterface::allInterfaces()` | `GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, ...)` | `iphlpapi.lib` |
| `QList` | `std::vector<自定义网卡结构>` | 无开销 |
| `QNetworkInterface::flags()` | `IP_ADAPTER_ADDRESSES::IfType` / `OperStatus` | `IfType == IF_TYPE_ETHERNET_CSMACD` 等 |
| `QNetworkInterface::hardwareAddress()` | `PhysicalAddress` 转十六进制 | 已有 `ToHexString` 或手写 |
| `QNetworkAddressEntry::ip().toString()` | `inet_ntop` / `RtlIpv4AddressToStringExW` | IPv4 地址格式化 |
| `QAbstractSocket::IPv4Protocol` | `sockaddr->sa_family == AF_INET` | 协议族判断 |
| `QString::indexOf` | `std::wstring::find` | 网卡名称过滤 |

**新增数据结构（建议放在 `ip_util.h` 或独立头文件）**：

```cpp
struct NetworkAdapter {
    std::wstring name;              // 适配器名称
    std::wstring friendly_name;     // 友好名称
    std::string mac_address;        // MAC 地址（十六进制）
    std::vector<std::string> ipv4_addresses;
    std::vector<std::string> ipv4_broadcasts;
    bool is_up = false;
    bool is_loopback = false;
    bool is_wireless = false;
};
```

**关键代码骨架**：

```cpp
ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
ULONG family = AF_UNSPEC;
ULONG buf_len = 15000;
auto adapters = std::make_unique<BYTE[]>(buf_len);
auto* p_adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(adapters.get());

DWORD ret = GetAdaptersAddresses(family, flags, nullptr, p_adapters, &buf_len);
if (ret == ERROR_BUFFER_OVERFLOW) {
    adapters = std::make_unique<BYTE[]>(buf_len);
    p_adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(adapters.get());
    ret = GetAdaptersAddresses(family, flags, nullptr, p_adapters, &buf_len);
}

for (auto* adapter = p_adapters; adapter != nullptr; adapter = adapter->Next) {
    std::wstring friendly_name(adapter->FriendlyName);
    if (friendly_name.find(L"VMware") != std::wstring::npos) continue;
    if (friendly_name.find(L"Loopback") != std::wstring::npos) continue;
    // ...
}
```

**C++20 可用特性**：
- `std::make_unique<BYTE[]>(buf_len)`（C++14 已有，但动态数组 `unique_ptr` 在 C++20 更完善）。
- `std::format` 格式化 MAC 地址字符串。

---

### 2.7 `qwidget_helper.h` / `qwidget_helper.cpp`

**现状**：直接操作 `QWidget`，提供 `GetHWND(QWidget*)` 和 `SetBorderInFullScreen(QWidget*, bool)`。

**改造方案**：**保留文件但接口改为 `HWND`**。因为项目中去 Qt 后不再有 `QWidget`，调用方若仍需 DWM 边框设置，应直接传入 `HWND`。

```cpp
// qwidget_helper.h
#ifdef WIN32
#include <Windows.h>

namespace tc {
    void SetBorderInFullScreen(HWND hwnd, bool hasBorder);
    // 删除 GetHWND(QWidget*)，调用方直接传 hwnd
}
#endif
```

**注意**：此文件目前仍被 Qt 调用方使用。去 Qt 化后，调用方（如 `GammaRay` 主工程）需同步修改。若确认无调用方再需要，可直接删除。

---

### 2.8 `win32/dxgi_mon_detector.cpp`

**现状**：使用 `QGuiApplication::primaryScreen()->geometry()` 获取主屏分辨率，与 DXGI 输出做匹配。

**改造方案**：

| 原 Qt 类/函数 | Win32 替代 | 说明 |
|---|---|---|
| `QGuiApplication::primaryScreen()` | `EnumDisplayMonitors` + `GetMonitorInfoW` | 或直接用 `GetSystemMetrics` |
| `QScreen::geometry()` | `MONITORINFO::rcMonitor` | 主屏通常为 `(0,0)` 起始 |

**简化做法**：主显示器在 Windows 上通常是 Desktop Coordinates 为 `(0, 0)` 的那个，可直接默认匹配：

```cpp
void DxgiMonitorDetector::DetectAdapters() {
    infos_.clear();

    // 获取主显示器信息（替代 QScreen）
    POINT ptZero = {0, 0};
    HMONITOR hPrimary = MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monInfo = {sizeof(MONITORINFO)};
    RECT primaryRect = {};
    if (GetMonitorInfoW(hPrimary, &monInfo)) {
        primaryRect = monInfo.rcMonitor;
    }

    // ... DXGI 枚举逻辑不变 ...
    if (info.rect.left == primaryRect.left && info.rect.top == primaryRect.top
        && info.width == (primaryRect.right - primaryRect.left)
        && info.height == (primaryRect.bottom - primaryRect.top)) {
        info.primary = true;
    }
}
```

---

### 2.9 `qrcode/qr_generator.h` / `qrcode/qr_generator.cpp`

**现状**：返回 `QPixmap`，内部用 `QImage` + `QPainter` 绘制二维码像素。

**改造方案**：核心二维码生成库 `qrcodegen` 是独立的纯 C++，只需替换图像容器。

**选项 A（推荐）**：返回裸像素 buffer，由调用方决定如何渲染。

```cpp
// qr_generator.h
struct QRImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;   // 每像素 4 字节 RGBA
};

class QrGenerator {
public:
    static QRImage GenQRImage(const std::string& message, int qr_size = -1);
};
```

```cpp
QRImage QrGenerator::GenQRImage(const std::string& message, int qr_size) {
    auto segs = QrSegment::makeSegments(message.c_str());
    QrCode qr = QrCode::encodeSegments(segs, QrCode::Ecc::LOW, 5, 15, 1, false);
    int size = qr.getSize();
    QRImage img;
    img.width = size;
    img.height = size;
    img.rgba.resize(size * size * 4, 0xFF);  // 默认白色，不透明

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qr.getModule(x, y)) {
                auto* pixel = &img.rgba[(y * size + x) * 4];
                pixel[0] = 0;   // R
                pixel[1] = 0;   // G
                pixel[2] = 0;   // B
                pixel[3] = 0xFF;// A
            }
        }
    }

    // 若需要缩放，调用方自行处理（如 GDI+ StretchBlt、D3D 纹理缩放等）
    // 或在此用简单的最近邻插值放大到 qr_size
    if (qr_size != -1 && qr_size != size) {
        // 实现最近邻缩放...
    }
    return img;
}
```

**选项 B**：若调用方必须收到 Windows 位图句柄，可返回 `HBITMAP`：

```cpp
static HBITMAP GenQRBitmap(const std::string& message, int qr_size);
```

使用 `CreateDIBSection` 创建 DIB，填充像素后返回。调用方用 `DeleteObject` 释放。

> **注意**：`qr_generator.h` 的接口变更会影响所有调用方。当前项目中搜索 `GenQRPixmap` 使用位置，需同步修改。

---

### 2.10 `win32/win_helper.cpp`

**现状**：`IsDllInjected` 使用 `QString::compare(..., Qt::CaseInsensitive)`；`InjectDll` 使用 `QProcess` + `QStringList`。

**改造方案**：

| 原 Qt 类/函数 | C++20 / Win32 替代 | 说明 |
|---|---|---|
| `QString::compare(..., Qt::CaseInsensitive)` | `::_wcsicmp` 或 `CompareStringOrdinal` | 大小写不敏感宽字符比较 |
| `QProcess` | `CreateProcessW` + `WaitForSingleObject` | 同步等待注入器完成 |
| `QStringList` / `QString::number` | `std::vector<std::wstring>` / `std::to_wstring` | 参数列表构建 |

**关键代码变更**：

```cpp
// IsDllInjected 中
std::wstring wname = StringUtil::ToWString(name);
std::wstring wtarget = StringUtil::ToWString(is_x86 ? x86_dll_name : x64_dll_name);
if (_wcsicmp(wname.c_str(), wtarget.c_str()) == 0) {
    ret_val = true;
    break;
}

// InjectDll 中
std::wstring injector = StringUtil::ToWString(is_x86.value_ ? kInjector32 : kInjector64);
std::wstring target_dll = StringUtil::ToWString(is_x86.value_ ? x86_dll_name : x64_dll_name);
std::wstring pid_str = std::to_wstring(pid);

std::wstring cmd_line = std::format(L"\"{}\" \"{}\" 0 {}", injector, target_dll, pid_str);

STARTUPINFOW si = {sizeof(si)};
PROCESS_INFORMATION pi = {};
if (CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}
```

---

### 2.11 `win32/audio_device_helper.cpp`

**现状**：使用 `QString::fromStdWString(...).toStdString()` 将 `LPWSTR` 转为 `std::string`。

**改造方案**：

```cpp
// 删除 #include <QString>
// 改为：
audio_device.id_ = StringUtil::ToUTF8(deviceId);
audio_device.name_ = StringUtil::ToUTF8(deviceName);
```

这是**最简单的替换**，仅删除 include 并替换一行代码。

---

## 三、C++20 特性使用建议

改造过程中，可充分利用以下 C++20 能力提升代码质量：

| 特性 | 应用场景 | 示例 |
|---|---|---|
| `std::format` / `std::format_to` | 字符串拼接、日志格式化、命令行构建 | `std::format(L"explorer /select,\"{}\"", path)` |
| `std::ranges::find_if` / `std::views::filter` | 网卡列表过滤、进程列表查找 | `std::ranges::find_if(adapters, [](auto& a){ return a.is_up; })` |
| `std::span<uint8_t>` | 图像像素 buffer、文件读写 buffer 传递 | 替代原始 `char*` + size 组合 |
| `std::jthread` | 若后续需要异步 IO 线程 | 替代 `std::thread`，自动 join |
| `std::u8string` | UTF-8 路径/文本处理（谨慎） | 与 `std::filesystem::u8path` 配合 |
| 概念（Concepts） | 模板约束（如通用 buffer 接口） | `template<std::contiguous_iterator Iter>` |

> **注意**：`std::u8string` 在 C++20 中与 `std::string` 不直接兼容（`char8_t` vs `char`），若项目已大量混用 UTF-8 `std::string`，建议继续使用 `std::string` + `StringUtil::ToUTF8`，避免类型爆炸。

---

## 四、改造优先级与工作量

| 优先级 | 文件 | 预估工时 | 难度 |
|---|---|---|---|
| P0 | `string_util.cpp` | 10 min | ⭐ |
| P0 | `audio_device_helper.cpp` | 10 min | ⭐ |
| P0 | `auto_start.h/cpp` | 30 min | ⭐⭐ |
| P0 | `file_util.cpp` | 30 min | ⭐⭐ |
| P0 | `folder_util.h/cpp` | 1 h | ⭐⭐ |
| P0 | `file.h/cpp` | 1.5 h | ⭐⭐⭐ |
| P1 | `dxgi_mon_detector.cpp` | 30 min | ⭐⭐ |
| P1 | `win_helper.cpp` | 1 h | ⭐⭐ |
| P1 | `hardware.cpp` / `ip_util.cpp` | 2 h | ⭐⭐⭐ |
| P2 | `qwidget_helper.h/cpp` | 20 min / 或直接删除 | ⭐ |
| P2 | `qrcode/qr_generator.h/cpp` | 1 h | ⭐⭐ |
| — | `CMakeLists.txt` | 20 min | ⭐ |

**总计**：约 **8~10 小时**（不含编译调试与调用方同步修改）。

---

## 五、风险点与注意事项

1. **file.cpp 的 IO 性能**：`std::fstream` 在 Windows MSVC 上默认使用 `FILE*` 实现，性能与 `QFile` 相近。若项目对文件 IO 有极致性能要求（如大文件连续读写），建议直接用 `CreateFileW` + `ReadFile`/`WriteFile`。

2. **路径编码**：Qt 的 `QString` 内部使用 UTF-16，`std::filesystem::path` 在 Windows 上默认也是宽字符（`wchar_t`），编码转换风险较低。但需确保 `StringUtil::ToWString` / `ToUTF8` 的实现在去 Qt 后依然可用（它们本身不依赖 Qt）。

3. **`qr_generator` 接口变更影响范围**：当前返回 `QPixmap`，去 Qt 后所有调用方（如 GammaRay 主工程中的 UI 代码）需同步修改。建议先定义好新接口（如 `QRImage` 裸像素结构），再批量替换。

4. **`qwidget_helper` 的归属**：该文件本质是对 Qt Widget 的 Win32 扩展。如果 `tc_common_new` 去 Qt 后仍被 Qt 项目引用，可考虑将该文件**移出** `tc_common_new`，放入 `tc_qt_widget` 等 Qt 专属库中。

5. **测试覆盖**：改造后需重点测试：
   - 文件读写（大文件、中文路径、特殊符号路径）
   - 目录递归遍历（权限不足目录的跳过）
   - 网卡枚举（多网卡、虚拟网卡过滤、IPv4/IPv6 混合环境）
   - 开机自启注册表读写

---

## 六、测试策略（gtest）

> **原则**：`tc_common_new` 中所有可单元测试的逻辑，**每改造一个模块，同步补充/更新对应的 gtest**。不允许"先改完再补测试"。

### 6.1 现有测试基础

`tc_common_new/tests/` 目录下已有：
- `test_common` — 通用工具测试
- `test_http` — HTTP 客户端测试
- `test_cpu` — CPU 信息测试

这些测试目前链接了 `Qt6::Core Qt6::Core5Compat`。去 Qt 化过程中需同步移除测试自身的 Qt 依赖，并扩展用例覆盖新改造的逻辑。

### 6.2 新增/扩展测试清单

| 被改造模块 | 测试文件名 | 测试重点 | 备注 |
|---|---|---|---|
| `string_util.cpp` | `test_string_util.cpp` | `Trim` 对空格/制表符/换行、空串、全空白串、无空白串的处理 | 最简单，作为改造热身 |
| `file.h/cpp` | `test_file.cpp` | 打开/关闭、读写偏移、追加模式、大文件（>4GB）、中文路径、特殊字符路径 | 需用临时目录 `std::filesystem::temp_directory_path` |
| `folder_util.cpp` | `test_folder_util.cpp` | `CreateDir` 递归创建、`DeleteDir` 递归删除、`CopyDir` 过滤与覆盖、`VisitFiles/VisitAll` 遍历与后缀过滤 | 每个用例前后清理临时目录 |
| `file_util.cpp` | `test_file_util.cpp` | `CopyFileExt` 覆盖/不覆盖、`ReName` 成功与失败路径、`GetFileNameFromPath` 各种分隔符 | 纯函数为主，易测试 |
| `auto_start.cpp` | `test_auto_start.cpp` | `SetAutoStart` 写入/删除注册表键后断言存在性；`NewLogonTask` 创建/删除计划任务后枚举验证 | 需管理员权限的用例标记 `DISABLED_` |
| `hardware.cpp` / `ip_util.cpp` | `test_network_adapter.cpp` | 扫描结果非空、过滤逻辑（无 VM/Loopback/VirtualBox/WSL）、MAC 地址格式、IPv4 有效性 | 依赖运行环境，用例设计为"存在性检查"而非固定值断言 |
| `dxgi_mon_detector.cpp` | `test_dxgi_mon_detector.cpp` | `DetectAdapters` 返回非空、主屏标记唯一且坐标为 `(0,0)` | 需 Windows + 显示器环境 |
| `qr_generator.cpp` | `test_qr_generator.cpp` | 生成图像尺寸正确、黑白像素分布符合二维码规律、不同 `qr_size` 缩放正确 | 不依赖 Qt，直接检查 `QRImage` 像素值 |
| `win_helper.cpp` | `test_win_helper.cpp` | `GetExeName` 对本进程断言、`GetModulePath` 路径存在性、`IsX86Arch` 返回合理值 | 部分用例依赖进程环境 |
| `audio_device_helper.cpp` | `test_audio_device.cpp` | `DetectAudioDevices` 返回非空、默认设备标记唯一 | 依赖音频设备 |

### 6.3 测试用例设计规范

1. **每个测试用例独立**：使用 `TEST()` 或 `TEST_F()`，禁止用例间共享可变状态。
2. **临时资源 RAII**：临时文件/目录用 `std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / std::format("tc_test_{}", rand());` 并在 `TearDown` 或作用域结束时 `remove_all`。
3. **环境敏感用例降级**：若测试依赖特定硬件/权限/环境，命名前缀加 `DISABLED_`，或放在 `TEST(...)` 内部做 `GTEST_SKIP()`：
   ```cpp
   TEST(NetworkAdapterTest, ScanIPs) {
       auto adapters = tc::IPUtil::ScanIPs();
       if (adapters.empty()) {
           GTEST_SKIP() << "No network adapter found, skip this test.";
       }
       EXPECT_FALSE(adapters[0].mac_address.empty());
   }
   ```
4. **边改边测节奏**：
   - **改前**：先阅读现有代码，理解边界行为，写下测试用例（此时测试依赖 Qt，编译通过但记录基线行为）。
   - **改中**：每替换一个 Qt API，立即运行对应测试模块，确保行为一致。
   - **改后**：所有测试绿灯后，再进入下一个文件。
5. **CMake 集成**：新增测试文件需在 `tc_common_new/tests/CMakeLists.txt` 中注册：
   ```cmake
   add_executable(test_string_util test_string_util.cpp)
   target_link_libraries(test_string_util PRIVATE tc_common_new GTest::gtest GTest::gtest_main)
   add_test(NAME test_string_util COMMAND test_string_util)
   ```

### 6.4 测试驱动的改造顺序建议

按"易测试 → 难测试"排序，降低前期心智负担：

```
1. string_util       (纯函数，秒级反馈)
2. file_util         (纯函数 + 文件系统，分钟级反馈)
3. folder_util       (目录操作，分钟级反馈)
4. file.h/cpp        (IO 核心，需仔细验证)
5. auto_start        (注册表，可 mock 或真机验证)
6. qr_generator      (像素断言，直观)
7. dxgi_mon_detector (硬件相关)
8. hardware / ip_util (硬件相关)
9. win_helper        (系统 API)
10. audio_device_helper (硬件相关)
```

---

## 七、验收标准

- [ ] `src/GammaRay/deps/tc_common_new` 目录下无 `#include <Q...>` 或 `#include <Qt...>`（`process_util.cpp` / `image_generator.cpp` 除外）。
- [ ] `CMakeLists.txt` 中不再 `find_package(Qt6 ...)` 和 `target_link_libraries(tc_common_new Qt6::...)`。
- [ ] 所有被改造文件在 Windows 下编译通过，**对应 gtest 全部通过**。
- [ ] 调用方（如 `GammaRay`、`panel_companion` 等）同步适配接口变更后，整体项目编译通过。
