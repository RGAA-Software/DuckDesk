# GammaRayRender 去 Qt 化改造方案

> **目标**：移除 `GammaRayRender.exe` 对 Qt6（Core / Core5Compat / Widgets / Network / WebSockets）的全部依赖。
> **范围**：`src/GammaRay/src/render` 模块及其插件系统。
> **前提**：`tc_common_new` 已完成去 Qt（已验证通过）。

---

## 一、现状全景分析

### 1.1 关键发现：没有 `Q_OBJECT` 宏

搜索整个 `src/GammaRay/src/render` 模块，**没有任何头文件包含 `Q_OBJECT` 宏**。这意味着：

- 5 个继承 `QObject` 的类均未经过 Qt MOC 处理
- **没有自定义信号/槽**（无 `signals:` / `slots:` 关键字）
- `QMetaObject::invokeMethod` 的使用全部是 **lambda 形式的异步投递**，不是传统信号槽机制
- 整个模块中唯一的 `connect()` 出现在 `AppTimer` 中（连接 `QTimer::timeout`）

**结论**：Qt 的使用深度比表面看起来更浅——主要是作为"线程任务调度器 + UI 容器 + 文件工具"，而非深度框架集成。

### 1.2 Qt 使用点完整清单

按模块分类，Qt API 使用情况如下：

#### 入口层

| 文件 | Qt API | 用途 |
|------|--------|------|
| `rd_main.cpp` | `QLockFile` | 单实例文件锁 |
| `rd_main.cpp` | `QMessageBox::critical` | 启动失败弹窗 |
| `rd_main.cpp` | `QDir::temp().absoluteFilePath` | 临时目录路径 |

#### 应用核心层

| 文件 | Qt API | 用途 |
|------|--------|------|
| `rd_app.h/cpp` | `QApplication` | 事件循环 (`app_->exec()`) |
| `rd_app.cpp` | `QString::fromStdWString` / `toStdWString` | 路径字符串转换 |
| `rd_app.cpp` | `QMetaObject::invokeMethod` | 全局任务投递到 UI 线程 |
| `rd_app.h` | `QObject` | `RdApplication` 基类 |

#### 上下文层

| 文件 | Qt API | 用途 |
|------|--------|------|
| `rd_context.h/cpp` | `QObject` | `RdContext` 基类 |
| `rd_context.cpp` | `QMetaObject::invokeMethod` | UI 线程任务投递 |
| `rd_context.cpp` | `QTimer::singleShot` | 延迟任务执行 |
| `rd_context.cpp` | `QLibrary` | 加载 `tc_global_id_generator.dll` |
| `rd_context.cpp` | `QCoreApplication::applicationDirPath` | 获取程序目录 |

#### 定时器层

| 文件 | Qt API | 用途 |
|------|--------|------|
| `app/app_timer.h/cpp` | `QObject` | `AppTimer` 基类 |
| `app/app_timer.cpp` | `QTimer` | 周期性定时器 |
| `app/app_timer.cpp` | `connect()` | 唯一一处 Qt 信号槽连接 |

#### 插件框架层

| 文件 | Qt API | 用途 |
|------|--------|------|
| `plugin_interface/gr_plugin_context.h/cpp` | `QObject` | `GrPluginContext` 基类 |
| `plugin_interface/gr_plugin_context.cpp` | `QMetaObject::invokeMethod` | 工作线程任务投递 |
| `plugin_interface/gr_plugin_context.cpp` | `QTimer::singleShot` | 延迟任务 |
| `plugin_interface/gr_plugin_interface.h` | `QObject` | `GrPluginInterface` 基类 |
| `plugin_interface/gr_plugin_interface.h` | `QWidget*` | 插件根窗口句柄 |
| `plugin_interface/gr_plugin_interface.h` | `QLabel`, `QHBoxLayout`, `QVBoxLayout` | 头文件包含（未全部使用） |
| `plugin_interface/gr_plugin_interface.h` | `QPixmap` | 图像显示 |
| `plugin_interface/gr_plugin_interface.cpp` | `QEvent` | `eventFilter` 参数 |
| `plugin_interface/gr_plugin_interface.cpp` | `QMetaObject::invokeMethod` | 任务投递 |
| `plugin_interface/gr_plugin_interface.cpp` | `QTimer::singleShot` | 延迟任务 |
| `plugin_interface/gr_plugin_interface.cpp` | `QStandardPaths` | 标准路径获取 |

#### 插件管理层

| 文件 | Qt API | 用途 |
|------|--------|------|
| `plugins/plugin_manager.h` | `QLibrary` | 插件动态加载 |
| `plugins/plugin_manager.cpp` | `QDir`, `QFile` | 目录遍历、文件存在检查 |
| `plugins/plugin_manager.cpp` | `QApplication` / `QCoreApplication` | 获取 `applicationDirPath` |
| `plugins/plugin_manager.cpp` | `QStandardPaths` | 标准数据路径 |

#### 平台适配层（Win32）

| 文件 | Qt API | 用途 |
|------|--------|------|
| `app/win/app_manager_win.cpp` | `QList`, `QString` | 游戏参数拆分（`QString::split`） |
| `app/win/win_render_message_loop.cpp` | `QApplication`, `QClipboard`, `QMimeData` | **已全部注释掉**，未实际使用 |
| `network/net_message_maker.cpp` | `QSysInfo` | 获取操作系统名称和版本 |

#### 其他插件

| 文件 | Qt API | 用途 |
|------|--------|------|
| `plugins/net_ws/ws_plugin.cpp` | `#include <QFile>` | **仅 include，未使用** |
| `plugins/net_ws/ws_server.cpp` | `#include <QApplication>` | **仅 include，未使用** |
| `plugins/plugin_net_event_router.cpp` | `#include <QApplication>` | **仅 include，未使用** |
| `plugins/frame_debugger/frame_debugger_plugin.h/cpp` | `QLabel`, `QPixmap`, `QImage` | 帧调试窗口 |

#### CMake 构建层

| 文件 | Qt 组件 | 说明 |
|------|---------|------|
| `src/render/CMakeLists.txt` | `Qt6::Core` | 核心库 |
| `src/render/CMakeLists.txt` | `Qt6::Core5Compat` | 兼容层 |
| `src/render/CMakeLists.txt` | `Qt6::Widgets` | 窗口部件 |
| `src/render/CMakeLists.txt` | `Qt6::Network` | 网络（**待确认是否直接使用**） |
| `src/render/CMakeLists.txt` | `Qt6::WebSockets` | WebSocket（**待确认是否直接使用**） |

### 1.3 网络层 Qt 依赖调研结论

对 `src/render/network/` 及所有 `.cpp/.h` 进行全文搜索：

- **未找到**任何直接使用 `QWebSocket`、`QTcpSocket`、`QUdpSocket`、`QNetworkAccessManager` 的代码
- `Qt6::Network` 和 `Qt6::WebSockets` 在 CMake 中链接，但 render 模块本身**未直接调用**这些 API
- **推测**：这些依赖可能是为了链接某些间接依赖的库（如第三方库依赖 Qt Network），或者是历史遗留
- **建议**：在阶段二中通过"移除链接 + 编译验证"的方式确认实际依赖范围

---

## 二、总体策略与分阶段路线图

### 2.1 核心策略

**渐进式移除，保持随时可编译、可运行。**

每一阶段结束后，`GammaRayRender.exe` 必须能够正常编译和运行。不追求一次性完成全部改造。

### 2.2 分阶段路线图

```
阶段一（基础设施去 Qt） ──→ 阶段二（网络层去 Qt） ──→ 阶段三（插件框架去 Qt） ──→ 阶段四（CMake 清理）
   2-3 人天                0.5-1 人天（调研为主）      3-5 人天（架构核心）          0.5 人天
```

| 阶段 | 目标 | 关键改动 | 风险等级 |
|------|------|---------|---------|
| **阶段一** | 替换所有"纯基础设施"Qt API | `QLockFile`→命名互斥体, `QApplication`→原生消息循环, `QMetaObject::invokeMethod`→任务队列, `QTimer`→`std::thread` 定时器, `QLibrary`→`LoadLibraryW`, `QString/QDir/QFile`→`std::filesystem` | 🟡 中 |
| **阶段二** | 确认并移除网络层 Qt 依赖 | 移除 `Qt6::Network` 和 `Qt6::WebSockets` 链接，验证编译 | 🟢 低 |
| **阶段三** | 插件框架去 Qt（最大难点） | 重新设计插件 UI 抽象，将 `QWidget*` 替换为平台抽象句柄 | 🔴 高 |
| **阶段四** | CMake 清理与最终验证 | 删除所有 `find_package(Qt6)`、`target_link_libraries(Qt6::...)`，全量回归测试 | 🟡 中 |

---

## 三、阶段一：基础设施去 Qt（详细方案）

### 3.1 `rd_main.cpp` — 入口层

**当前 Qt 依赖**：
```cpp
#include <QLockFile>
#include <QMessageBox>
#include <QDir>
```

**改造方案**：

| 原 Qt API | C++20 / Win32 替代 | 说明 |
|---|---|---|
| `QLockFile` | `CreateMutexW`（命名互斥体） | `Global\GammaRayRender_InstanceLock_{pid}` |
| `QMessageBox::critical` | `MessageBoxW(NULL, text, title, MB_OK \| MB_ICONERROR)` | 原生 Win32 弹窗 |
| `QDir::temp().absoluteFilePath` | `GetTempPathW` + `std::filesystem::path` | 标准 Win32 API |

**关键代码变更**：

```cpp
// 改造前
std::shared_ptr<QLockFile> g_instance_lock = nullptr;
QString lock_path = QDir::temp().absoluteFilePath(lock_name.c_str());
g_instance_lock = std::make_shared<QLockFile>(lock_path);
if (!g_instance_lock->tryLock(2000)) {
    QMessageBox::critical(nullptr, "Start render failed!", reason);
}

// 改造后
HANDLE g_instance_mutex = NULL;
wchar_t temp_path[MAX_PATH];
GetTempPathW(MAX_PATH, temp_path);
auto lock_path = std::filesystem::path(temp_path) / StringUtil::ToWString(lock_name);

// 使用命名互斥体实现跨进程单实例
g_instance_mutex = CreateMutexW(NULL, TRUE, L"Global\\GammaRayRender_InstanceLock");
if (GetLastError() == ERROR_ALREADY_EXISTS) {
    MessageBoxW(NULL, StringUtil::ToWString(reason).c_str(), 
                L"Start render failed!", MB_OK | MB_ICONERROR);
    return -1;
}
```

---

### 3.2 `rd_app.h/cpp` — 应用核心层

**当前 Qt 依赖**：
- `QApplication`（事件循环）
- `QString`（路径转换）
- `QMetaObject::invokeMethod`（全局任务投递）
- `QObject`（基类）

**改造方案**：

#### 3.2.1 移除 `QObject` 基类

```cpp
// rd_app.h 改造前
class RdApplication : public std::enable_shared_from_this<RdApplication>, public QObject {
    Q_OBJECT  // 实际上不存在，确认过无此宏
};

// rd_app.h 改造后
class RdApplication : public std::enable_shared_from_this<RdApplication> {
    // QObject 的功能替换：
    // 1. 内存管理：Qt 父子对象树未使用，无需替换
    // 2. invokeMethod：见下方任务队列方案
};
```

#### 3.2.2 替换 `QApplication` 为原生消息循环

`RdApplication` 当前使用 `QApplication::exec()` 作为事件循环：

```cpp
// 改造前
int Run() { return app_->exec(); }
```

替换为 Win32 消息循环 + 自定义任务队列：

```cpp
// rd_app.h 新增
class TaskQueue {
public:
    void PostTask(std::function<void()> task);
    void ExecuteAll();  // 在消息循环中调用
private:
    std::mutex mutex_;
    std::queue<std::function<void()>> tasks_;
};

// rd_app.h 修改
class RdApplication : public std::enable_shared_from_this<RdApplication> {
    // std::shared_ptr<QApplication> app_ = nullptr;  // 删除
    std::unique_ptr<TaskQueue> task_queue_;
    HWND message_hwnd_ = NULL;  // 消息窗口句柄，用于 PostThreadMessage 唤醒
};

// rd_app.cpp 改造后
int RdApplication::Run() {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        
        // 处理自定义任务队列
        task_queue_->ExecuteAll();
    }
    return (int)msg.wParam;
}
```

**注意**：render 模块已有 `WinMessageLoop` 类（`app/win/win_render_message_loop.cpp`），它使用原生 Win32 窗口消息处理（`WndProc`、`HWND`）。可以将 `QApplication` 的消息循环职责合并到现有的 `WinMessageLoop` 中，避免重复造轮子。

#### 3.2.3 替换 `QMetaObject::invokeMethod` 为任务队列

当前使用方式：
```cpp
void RdApplication::PostGlobalAppMessage(std::shared_ptr<AppMessage>&& msg) {
    QMetaObject::invokeMethod(this, [m = std::move(msg)]() {
        if (m->task_) { m->task_(); }
    });
}
```

所有 `QMetaObject::invokeMethod` 的调用都是**基于 lambda 的异步投递**，不涉及信号槽。可直接替换为向任务队列投递：

```cpp
void RdApplication::PostGlobalAppMessage(std::shared_ptr<AppMessage>&& msg) {
    auto task = [m = std::move(msg)]() {
        if (m->task_) { m->task_(); }
    };
    task_queue_->PostTask(std::move(task));
    // 唤醒消息循环
    if (message_hwnd_) {
        PostMessage(message_hwnd_, WM_APP + 1, 0, 0);
    }
}
```

`TaskQueue` 实现：

```cpp
class TaskQueue {
public:
    void PostTask(std::function<void()> task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }
    
    void ExecuteAll() {
        std::queue<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            local.swap(tasks_);
        }
        while (!local.empty()) {
            local.front()();
            local.pop();
        }
    }
private:
    std::mutex mutex_;
    std::queue<std::function<void()>> tasks_;
};
```

#### 3.2.4 替换 `QString` 为 `std::wstring`

```cpp
// 改造前
auto path = QString::fromStdWString(FolderUtil::GetProgramDataPath()) + "/gr_data";
std::string sp_name = ...;
if (!sp_->Init(path.toStdWString(), sp_name)) { ... }

// 改造后
auto path = FolderUtil::GetProgramDataPath() + L"/gr_data";
std::string sp_name = ...;
if (!sp_->Init(path, sp_name)) { ... }
```

---

### 3.3 `rd_context.h/cpp` — 上下文层

**当前 Qt 依赖**：
- `QObject` 基类
- `QMetaObject::invokeMethod`
- `QTimer::singleShot`
- `QLibrary`
- `QCoreApplication::applicationDirPath`

**改造方案**：

#### 3.3.1 移除 `QObject` + 替换 `invokeMethod`

与 `RdApplication` 相同，`RdContext` 的 `QMetaObject::invokeMethod` 全部是 lambda 投递，替换为 `TaskQueue` 模式。

```cpp
// rd_context.h 改造前
class RdContext : public QObject, public std::enable_shared_from_this<RdContext> {
    void PostUITask(std::function<void()>&& task);
    void PostDelayTask(int delay, std::function<void()>&& task);
};

// rd_context.h 改造后
class RdContext : public std::enable_shared_from_this<RdContext> {
    void PostUITask(std::function<void()>&& task);
    void PostDelayTask(int delay, std::function<void()>&& task);
private:
    // 使用与 RdApplication 共享的任务队列，或独立队列
    std::shared_ptr<TaskQueue> ui_task_queue_;
    std::shared_ptr<DelayTaskScheduler> delay_scheduler_;
};
```

#### 3.3.2 替换 `QTimer::singleShot` 为延迟任务调度器

```cpp
// 改造前
void RdContext::PostDelayTask(int delay, std::function<void()>&& task) {
    QTimer::singleShot(delay, [weak_self, task]() {
        auto self = weak_self.lock();
        if (!self || self->exiting_) { return; }
        task();
    });
}

// 改造后
class DelayTaskScheduler {
public:
    void Schedule(int delay_ms, std::function<void()> task);
    void Stop();
private:
    struct DelayedTask {
        std::chrono::steady_clock::time_point execute_at;
        std::function<void()> task;
    };
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<DelayedTask> tasks_;
    std::jthread worker_;
};

void RdContext::PostDelayTask(int delay, std::function<void()>&& task) {
    auto weak_self = weak_from_this();
    delay_scheduler_->Schedule(delay, [weak_self, t = std::move(task)]() {
        auto self = weak_self.lock();
        if (!self || self->exiting_) { return; }
        t();
    });
}
```

`DelayTaskScheduler` 可用最小堆（`std::priority_queue`）管理定时任务，单后台线程等待最近的任务到期时间。

#### 3.3.3 替换 `QLibrary` 为 `LoadLibraryW`

```cpp
// 改造前
auto id_generator_path = QCoreApplication::applicationDirPath() + "/tc_global_id_generator.dll";
auto library = new QLibrary(id_generator_path);
if (!library->load()) { ... }
g_fn_gen_next_global_id = (FnGenNextGlobalId)library->resolve("GenNextGlobalId");

// 改造后
auto exe_dir = Win32Helper::GetExeDirectory();  // 项目已有工具函数
auto id_generator_path = exe_dir + L"\\tc_global_id_generator.dll";
HMODULE hmod = LoadLibraryW(id_generator_path.c_str());
if (!hmod) {
    LOGE("Load global id generator failed: {}, error: {}",
         StringUtil::ToUTF8(id_generator_path), GetLastError());
    return false;
}
g_fn_gen_next_global_id = (FnGenNextGlobalId)GetProcAddress(hmod, "GenNextGlobalId");
// 注意：hmod 生命周期管理，建议在 RdContext 中保存 HMODULE，析构时 FreeLibrary
```

#### 3.3.4 替换 `QCoreApplication::applicationDirPath`

项目已有多个工具函数可获取程序目录：
- `tc_common_new` 中的 `Win32Helper::GetExeDirectory()`（假设存在，或新实现）
- 直接使用 `GetModuleFileNameW(NULL, ...)` + `std::filesystem::path::parent_path()`

```cpp
static std::wstring GetExeDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().wstring();
}
```

---

### 3.4 `app/app_timer.h/cpp` — 定时器层

**当前 Qt 依赖**：
- `QObject` 基类
- `QTimer`
- `connect()`（唯一信号槽连接）

**改造方案**：

`AppTimer` 管理多个周期性定时器（`k1Second`、`k5Seconds` 等），每个对应一个 `QTimer`。

```cpp
// app_timer.h 改造前
class AppTimer : public QObject {
    std::map<AppTimerDuration, std::shared_ptr<QTimer>> timers_;
};

// app_timer.h 改造后
class AppTimer {
public:
    void Start(AppTimerDuration duration);
    void Stop(AppTimerDuration duration);
    void StopAll();
    void RegisterListener(AppTimerDuration duration, const std::function<void()>& listener);
private:
    struct TimerInstance {
        int interval_ms;
        std::vector<std::function<void()>> listeners;
        std::jthread worker;
        std::atomic_bool running{false};
    };
    std::map<AppTimerDuration, std::unique_ptr<TimerInstance>> timers_;
};
```

每个 `TimerInstance` 的后台线程逻辑：

```cpp
void TimerInstance::Run() {
    running = true;
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        if (!running) break;
        for (auto& listener : listeners) {
            listener();
        }
    }
}
```

**注意**：原 `QTimer` 的回调在 UI 线程执行（因为 `QTimer` 属于 `QObject`，绑定到创建它的线程）。改造后需要明确：
- 如果定时器回调**必须**在 UI 线程执行，则后台线程只负责计时，实际回调通过 `PostMessage`/`TaskQueue` 投递到 UI 线程
- 如果定时器回调**可以**在后台线程执行，则上述方案直接可用

需审查每个 `AppTimer` 监听器的实现，确认线程安全要求。

---

### 3.5 `plugins/plugin_manager.cpp` — 插件管理层

**当前 Qt 依赖**：
- `QDir`、`QFile`
- `QApplication` / `QCoreApplication`
- `QStandardPaths`
- `QLibrary`

**改造方案**：

| 原 Qt API | C++20 / Win32 替代 |
|---|---|
| `QDir::exists()` | `std::filesystem::exists()` |
| `QFile::exists()` | `std::filesystem::exists()` |
| `QDirIterator` | `std::filesystem::directory_iterator` / `recursive_directory_iterator` |
| `QCoreApplication::applicationDirPath()` | `GetExeDirectory()`（见 3.3.4） |
| `QStandardPaths::writableLocation(AppDataLocation)` | `SHGetKnownFolderPath(FOLDERID_RoamingAppData, ...)` |
| `QLibrary` | `LoadLibraryW` / `GetProcAddress`（见 3.3.3） |

`QLibrary` 替换为 `DynamicLibrary` 封装类（可在 `tc_common_new` 中新增，供多处复用）：

```cpp
// tc_common_new/win32/dynamic_library.h
class DynamicLibrary {
public:
    explicit DynamicLibrary(const std::wstring& path);
    ~DynamicLibrary();
    bool Load();
    void* GetSymbol(const std::string& name);
    std::string GetErrorString();
private:
    std::wstring path_;
    HMODULE handle_ = NULL;
};
```

---

### 3.6 `network/net_message_maker.cpp`

**当前 Qt 依赖**：`QSysInfo`

**改造方案**：

```cpp
// 改造前
static QString os_name;
if (os_name.isEmpty()) {
    auto product_type = QSysInfo::productType();       // 如 "windows"
    auto product_version = QSysInfo::productVersion(); // 如 "11"
    os_name = product_type + " " + product_version;
}

// 改造后
static std::string os_name;
if (os_name.empty()) {
    // 使用 RtlGetVersion 或 WMI 获取精确版本
    // 简化方案：使用项目已有的 OS 版本获取工具，或直接读取注册表
    os_name = Win32Helper::GetProductName();  // 从注册表 "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" 读取 "ProductName"
}
```

注册表读取方案（无需 Qt）：

```cpp
std::string GetWindowsProductName() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buf[256];
        DWORD size = sizeof(buf);
        if (RegQueryValueExA(hKey, "ProductName", NULL, NULL, (LPBYTE)buf, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::string(buf);
        }
        RegCloseKey(hKey);
    }
    return "Windows";
}
```

---

### 3.7 `app/win/app_manager_win.cpp`

**当前 Qt 依赖**：`QList`、`QString`

**改造方案**：

仅用于游戏参数字符串拆分：

```cpp
// 改造前
auto game_args = QString::fromStdString(settings_->app_.game_arguments_);
auto split_game_args = game_args.split(' ');
for (const auto& arg: split_game_args) {
    args.push_back(arg.toStdString());
}

// 改造后
auto game_args = settings_->app_.game_arguments_;
std::istringstream iss(game_args);
std::string arg;
while (iss >> arg) {  // 自动按空白字符拆分，处理多个空格
    args.push_back(arg);
}
```

---

### 3.8 未实际使用的 Qt Include 清理

以下文件中的 Qt include **仅存在但未实际使用**，可直接删除：

| 文件 | 待删除的 include |
|------|-----------------|
| `plugins/net_ws/ws_plugin.cpp` | `#include <QFile>` |
| `plugins/net_ws/ws_server.cpp` | `#include <QApplication>` |
| `plugins/plugin_net_event_router.cpp` | `#include <QApplication>` |
| `app/win/win_render_message_loop.cpp` | `#include <QApplication>`, `#include <QMimeData>`, `#include <QClipboard>`（相关代码已全部注释） |

---

## 四、阶段二：网络层 Qt 依赖确认与移除

### 4.1 验证方法

由于全文搜索未找到 `src/render` 中直接使用 Qt Network / WebSocket API 的代码，采用"编译验证法"确认依赖范围：

1. 在 `src/render/CMakeLists.txt` 中注释掉：
   ```cmake
   # find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS ...)
   # Qt6::Network
   # Qt6::WebSockets
   ```
2. 重新运行 `build_official.bat`
3. 观察链接错误：
   - 如果链接成功 → 确认无直接依赖，可安全移除
   - 如果出现未定义符号 → 追踪符号来源，判断是 render 模块自身使用还是第三方库传递依赖

### 4.2 如果存在间接依赖

如果第三方库（如某个 websocket 库）依赖 Qt Network，可选方案：

| 方案 | 说明 | 工作量 |
|------|------|--------|
| A. 保留 Qt Network 链接 | 仅移除 render 自身的 Qt 使用，保留链接供第三方库使用 | 0 |
| B. 替换第三方库 | 将依赖 Qt 的第三方库替换为不依赖 Qt 的等价库（如 `boost::beast` 替代 `QWebSocket`） | 视库而定 |

**建议**：优先验证方案 A（保留链接）是否可行。如果 `GammaRayRender.exe` 的运行不依赖 Qt Network 的运行时 DLL（即只是链接但不调用），则阶段二目标已达成。

---

## 五、阶段三：插件框架去 Qt（最大难点）

### 5.1 问题分析

`GrPluginInterface` 是**所有插件的基类**，其中硬编码了 Qt UI 依赖：

```cpp
class GrPluginInterface : public QObject {
    QWidget* root_widget_ = nullptr;           // 插件根窗口
    bool eventFilter(QObject*, QEvent*) override;  // 拦截 Close 事件
    void ShowRootWidget();  // 调用 QWidget::show()
    void HideRootWidget();  // 调用 QWidget::hide()
};
```

**影响范围**：
- 所有插件都继承 `GrPluginInterface`，因此所有插件都**隐式依赖 Qt Widgets**
- `frame_debugger` 插件实际使用 `QLabel` + `QPixmap` 创建调试窗口
- 即使 `GammaRayRender.exe` 本身去 Qt，只要插件系统基于 `QWidget*`，就必须链接 `Qt6::Widgets`

### 5.2 解决方案选型

#### 方案 A：平台原生窗口抽象（推荐）

将 `QWidget*` 替换为跨平台的窗口句柄抽象。

```cpp
// plugin_interface/gr_plugin_interface.h 改造后
#ifdef _WIN32
    using PluginWindowHandle = HWND;
#else
    using PluginWindowHandle = void*;  // Linux: X11 Window 或 Wayland surface
#endif

class GrPluginInterface {
public:
    // UI 接口
    PluginWindowHandle GetRootWindow();
    void ShowRootWindow();
    void HideRootWindow();
    void OnRootWindowClose();  // 替代 eventFilter 的 Close 事件拦截
    
protected:
    PluginWindowHandle root_window_ = NULL;
};
```

**Win32 实现**：

```cpp
void GrPluginInterface::ShowRootWindow() {
    if (root_window_) { ShowWindow((HWND)root_window_, SW_SHOW); }
}

void GrPluginInterface::HideRootWindow() {
    if (root_window_) { ShowWindow((HWND)root_window_, SW_HIDE); }
}
```

**插件侧适配**：

插件内部可以选择：
1. **继续使用 Qt**：插件自行创建 `QWidget`，通过 `QWidget::winId()` 获取 `HWND` 返回给框架
2. **使用原生 Win32**：直接用 `CreateWindowEx` 创建窗口

```cpp
// 某插件继续使用 Qt 的实现（兼容模式）
PluginWindowHandle MyPlugin::GetRootWindow() {
    if (!root_window_) {
        auto widget = new QWidget();
        widget->installEventFilter(this);  // 插件内部处理 Close 事件
        root_window_ = (PluginWindowHandle)widget->winId();
    }
    return root_window_;
}
```

**优点**：
- 框架本身不依赖 Qt
- 现有 Qt 插件可渐进迁移（先返回 HWND，内部仍用 Qt）
- 新插件可直接用原生 API

**缺点**：
- `frame_debugger` 插件使用 `QPixmap` 显示图像，需要重写为 GDI+ 或 Direct2D
- 插件内部若继续使用 Qt，则该插件 DLL 仍需携带 Qt 依赖（但这是插件自己的依赖，不是 `GammaRayRender.exe` 的）

#### 方案 B：完全移除插件 UI 能力

如果所有插件都不需要显示独立窗口，可直接删除 `root_widget_` 相关接口。

**适用性判断**：需审查所有现有插件，确认是否有插件依赖 `ShowRootWidget`/`HideRootWidget`。目前确认 `frame_debugger` 插件使用了 QWidget。

**结论**：不可行，至少 `frame_debugger` 需要 UI。

#### 方案 C：保留 Qt 插件系统，Render 本身不链接 Qt Widgets

`GammaRayRender.exe` 编译时不链接 `Qt6::Widgets`，但运行时通过 `LoadLibrary` 加载 Qt DLL。

**实现方式**：
- 插件接口头文件中，将 `QWidget*` 改为 `void*`
- 框架侧只保存/传递 `void*`，不进行任何 Qt 操作
- 插件侧将 `void*` 转回 `QWidget*`

**缺点**：
- `QObject` 的信号槽、`eventFilter` 等功能仍然需要 Qt 头文件
- `GrPluginInterface` 基类仍需要 `QObject` 来支持 `QMetaObject::invokeMethod`
- 不彻底，治标不治本

### 5.3 推荐方案：A（平台原生窗口抽象）

阶段三的实施步骤：

1. **定义 `PluginWindowHandle`**：在 `gr_plugin_interface.h` 中添加平台抽象类型
2. **替换 `QWidget*` 为 `PluginWindowHandle`**：修改 `root_widget_` 成员和相关接口
3. **移除 `eventFilter`**：将 Close 事件拦截改为 `OnRootWindowClose()` 虚函数，由插件自行实现
4. **移除 `QObject` 基类**：同时移除 `GrPluginContext` 的 `QObject` 基类
5. **替换 `QMetaObject::invokeMethod`**：在插件基类中使用 `TaskQueue`（与阶段一统一）
6. **重写 `frame_debugger` 插件**：将 `QLabel` + `QPixmap` 替换为 GDI+ 或原生窗口绘制
7. **迁移现有插件**：每个插件适配新接口（若插件继续使用 Qt 内部实现，则最小改动）

### 5.4 `frame_debugger` 插件重写方案

当前使用 Qt 显示 RGBA 图像：

```cpp
static QPixmap RgbaToPixmap(const uint8_t* data, int width, int height) {
    QImage img(data, width, height, QImage::Format_RGBA8888);
    auto pixmap = QPixmap::fromImage(img.copy());
    return pixmap.scaled(960, 540);
}
```

替换为 GDI+ 方案：

```cpp
// 使用 GDI+ Bitmap 直接显示
#include <gdiplus.h>

static Gdiplus::Bitmap* RgbaToBitmap(const uint8_t* data, int width, int height) {
    Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData bmpData;
    bmp->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmpData);
    memcpy(bmpData.Scan0, data, width * height * 4);
    bmp->UnlockBits(&bmpData);
    return bmp;
}

// 在 WM_PAINT 中绘制
void FrameDebuggerWindow::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    Gdiplus::Graphics graphics(hdc);
    graphics.DrawImage(bitmap_, 0, 0, 960, 540);
    EndPaint(hwnd, &ps);
}
```

---

## 六、阶段四：CMake 清理与最终验证

### 6.1 CMakeLists.txt 修改清单

#### `src/render/CMakeLists.txt`

```cmake
# 删除以下内容
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Core Core5Compat WebSockets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Core Core5Compat WebSockets Network)

target_link_libraries(main Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Core5Compat
    Qt6::WebSockets Qt6::Network)

# 添加 Win32 系统库（阶段一中逐步补充）
if (WIN32)
    target_link_libraries(main
        user32.lib gdi32.lib shell32.lib
        advapi32.lib shlwapi.lib
        gdiplus.lib  # frame_debugger 等插件使用 GDI+
    )
endif()
```

#### 各插件 CMakeLists.txt

逐个检查 `src/render/plugins/*/CMakeLists.txt`，删除 Qt 查找和链接。

### 6.2 最终验证清单

| 验证项 | 方法 |
|--------|------|
| 编译通过 | `build_official.bat` 完整构建 |
| 无 Qt DLL 依赖 | `dumpbin /dependents GammaRayRender.exe` 检查输出中无 `Qt6*.dll` |
| 单实例锁 | 启动两个实例，第二个应弹出错误提示 |
| 消息循环 | 长时间运行，确认无消息堆积、无 CPU 空转 |
| 定时器 | 确认 1s/5s/60s 定时器正常触发 |
| 插件加载 | 确认所有插件正常加载、初始化、销毁 |
| 插件 UI | 确认 `frame_debugger` 等带 UI 的插件窗口正常显示/隐藏 |
| 剪贴板 | 若剪贴板功能恢复，验证跨设备剪贴板同步 |
| 性能 | 对比改造前后 CPU/内存占用，确认无退化 |

---

## 七、各 Qt 组件替换技术详解

### 7.1 线程任务投递（替代 `QMetaObject::invokeMethod`）

**Qt 原理解析**：
`QMetaObject::invokeMethod(obj, lambda, Qt::QueuedConnection)` 的本质是：
1. 将 lambda 包装为 `QMetaCallEvent`
2. 通过 `QCoreApplication::postEvent` 投递到目标线程的事件队列
3. 目标线程的 `QEventLoop` 在 `processEvents` 时取出并执行

**替代方案**：
```cpp
// 统一任务队列（单例或注入）
class TaskQueue {
public:
    void PostTask(std::function<void()> task);
    void ExecuteAll();  // 在主线程消息循环中调用
private:
    std::mutex mutex_;
    std::queue<std::function<void()>> tasks_;
};

// 唤醒机制：PostMessage 到消息窗口
void TaskQueue::PostTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }
    PostMessage(g_message_hwnd, WM_APP + 1, 0, 0);  // 唤醒消息循环
}
```

### 7.2 定时器（替代 `QTimer`）

**方案对比**：

| 方案 | 精度 | 资源占用 | 适用场景 |
|------|------|---------|---------|
| `std::jthread` + `sleep_for` | ~15ms（受系统调度影响） | 每个定时器一个线程 | 低频定时器（>=1s） |
| `WaitableTimer` (Win32) | ~1ms | 内核对象 | 高精度定时 |
| 统一调度器 + 最小堆 | 一个线程管理所有定时器 | 最优 | 多定时器场景 |

**推荐**：统一调度器方案（单后台线程 + `std::priority_queue`）。

```cpp
class TimerScheduler {
    struct Entry {
        std::chrono::steady_clock::time_point next_fire;
        int interval_ms;
        std::function<void()> callback;
        bool operator>(const Entry& other) const { return next_fire > other.next_fire; }
    };
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> heap_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::jthread worker_;
    
    void Run() {
        while (running_) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (heap_.empty()) {
                cv_.wait(lock);
                continue;
            }
            auto next = heap_.top().next_fire;
            if (cv_.wait_until(lock, next) == std::cv_status::timeout) {
                auto entry = heap_.top(); heap_.pop();
                lock.unlock();
                entry.callback();
                entry.next_fire += std::chrono::milliseconds(entry.interval_ms);
                lock.lock();
                heap_.push(entry);
            }
        }
    }
};
```

### 7.3 DLL 动态加载（替代 `QLibrary`）

已在 3.3.3 和 3.5 中描述。建议封装为 `tc_common_new` 中的 `DynamicLibrary` 类，供多处复用。

### 7.4 文件系统操作（替代 `QDir`/`QFile`/`QStandardPaths`）

统一使用 `std::filesystem`（C++17）+ Win32 API 补充：

| Qt API | 替代 |
|--------|------|
| `QDir::exists()` | `std::filesystem::exists()` |
| `QDir::mkpath()` | `std::filesystem::create_directories()` |
| `QDir::removeRecursively()` | `std::filesystem::remove_all()` |
| `QFile::copy()` | `std::filesystem::copy_file()` |
| `QStandardPaths::writableLocation(AppDataLocation)` | `SHGetKnownFolderPath(FOLDERID_RoamingAppData, ...)` |
| `QStandardPaths::writableLocation(TempLocation)` | `GetTempPathW()` |

---

## 八、风险评估与回滚策略

### 8.1 风险矩阵

| 风险项 | 概率 | 影响 | 缓解措施 |
|--------|------|------|---------|
| 消息循环替换后消息丢失/堆积 | 中 | 高 | 保留原 `QApplication` 实现为 `#ifdef USE_QT` 分支，随时切换 |
| 定时器精度下降导致功能异常 | 低 | 中 | 使用 `WaitableTimer` 做精度对比测试 |
| 插件 `QWidget*` 替换后现有插件崩溃 | 中 | 高 | 阶段三单独分支开发，逐个插件验证 |
| 网络层移除后发现隐藏依赖 | 中 | 高 | 阶段二先做"链接移除 + 编译验证"，不删代码 |
| 多线程任务投递引入死锁 | 低 | 高 | 严格审查 `mutex` 持有时间，使用 `lock_guard` |
| 构建系统改动导致其他目标失败 | 低 | 中 | CMake 修改后全量构建验证 |

### 8.2 回滚策略

1. **代码层面**：每个阶段独立分支（`feature/render-deqt-phase1`、`feature/render-deqt-phase2`...），`master` 分支始终保持可运行状态
2. **编译层面**：保留 CMake 中的 Qt 查找为条件编译：
   ```cmake
   option(USE_QT "Use Qt for render module" OFF)
   if (USE_QT)
       find_package(Qt6 ...)
   endif()
   ```
3. **功能层面**：关键接口提供双实现：
   ```cpp
   #ifdef USE_QT
       void PostUITask(std::function<void()>&& task) { 
           QMetaObject::invokeMethod(this, ...); 
       }
   #else
       void PostUITask(std::function<void()>&& task) { 
           task_queue_->PostTask(std::move(task)); 
       }
   #endif
   ```

---

## 九、工作量估算

### 9.1 阶段一：基础设施去 Qt

| 任务 | 文件数 | 工作量（人天） |
|------|--------|---------------|
| `rd_main.cpp` — 单实例锁、弹窗、路径 | 1 | 0.5 |
| `rd_app.h/cpp` — QApplication→消息循环, QObject移除, QString替换 | 2 | 1.5 |
| `rd_context.h/cpp` — QObject移除, invokeMethod→任务队列, QTimer→调度器, QLibrary→LoadLibrary | 2 | 1.5 |
| `app/app_timer.h/cpp` — QTimer→线程定时器 | 2 | 0.5 |
| `plugins/plugin_manager.cpp` — QDir/QFile→filesystem, QLibrary→DynamicLibrary | 1 | 0.5 |
| `network/net_message_maker.cpp` — QSysInfo→注册表 | 1 | 0.25 |
| `app/win/app_manager_win.cpp` — QString→std::string | 1 | 0.25 |
| 清理未使用的 Qt include | 4 | 0.25 |
| **小计** | | **~3 人天** |

### 9.2 阶段二：网络层去 Qt

| 任务 | 工作量（人天） |
|------|---------------|
| 移除 Qt6::Network/Qt6::WebSockets 链接，编译验证 | 0.5 |
| 若存在间接依赖，调研替代方案 | 0.5-2 |
| **小计** | **0.5-2 人天** |

### 9.3 阶段三：插件框架去 Qt

| 任务 | 工作量（人天） |
|------|---------------|
| 设计 `PluginWindowHandle` 抽象 | 0.5 |
| 修改 `GrPluginInterface` 基类（移除 QObject, QWidget*→HWND, eventFilter→OnRootWindowClose） | 1 |
| 修改 `GrPluginContext`（移除 QObject, invokeMethod→TaskQueue） | 0.5 |
| 重写 `frame_debugger` 插件（QLabel/QPixmap→GDI+/原生窗口） | 1-2 |
| 迁移其他现有插件适配新接口 | 0.5-1/插件 |
| **小计** | **3.5-5 人天 + 插件迁移** |

### 9.4 阶段四：CMake 清理与验证

| 任务 | 工作量（人天） |
|------|---------------|
| 清理所有 CMakeLists.txt 中的 Qt 引用 | 0.5 |
| 全量构建验证 | 0.25 |
| 运行时功能验证（单实例、定时器、插件、UI） | 0.5 |
| **小计** | **~1 人天** |

### 9.5 总计

| 阶段 | 工作量 | 并行度 |
|------|--------|--------|
| 阶段一 | 3 人天 | 串行 |
| 阶段二 | 0.5-2 人天 | 可与阶段一部分并行 |
| 阶段三 | 4-6 人天 | 依赖阶段一完成 |
| 阶段四 | 1 人天 | 串行 |
| **总计** | **~8-12 人天** | |

---

## 十、决策建议

### 10.1 是否推进？

**推荐推进阶段一和阶段二**，理由：
- 基础设施替换技术成熟，风险可控
- 可显著降低 `GammaRayRender.exe` 对 Qt Core/Widgets 的耦合
- 为后续架构演进打下基础（如 headless 模式、服务化改造）

**阶段三视业务优先级决定**，理由：
- 涉及插件系统架构变更，影响所有插件
- 如果当前没有"移除 Qt Widgets 链接"的强需求（如减小安装包体积、避免 Qt 版本冲突），可延后
- `frame_debugger` 插件的 GDI+ 重写需要额外测试图像渲染质量

### 10.2 最低有效方案（Minimum Viable De-Qt）

如果资源有限，优先完成以下子集，即可获得 80% 的去 Qt 收益：

1. ✅ `rd_main.cpp` — 移除 `QLockFile`、`QMessageBox`、`QDir`
2. ✅ `rd_app.cpp` — 消息循环替换（最大收益点）
3. ✅ `rd_context.cpp` — `QLibrary`→`LoadLibraryW`，`QTimer`→`std::thread`
4. ✅ `app_timer` — `QTimer`→`std::thread`
5. ✅ `plugin_manager.cpp` — `QDir`/`QFile`→`std::filesystem`
6. ✅ 清理未使用的 Qt include
7. ⏸️ 插件框架 `QWidget*` — 延后

此子集工作量约 **3-4 人天**，可将 `GammaRayRender.exe` 的核心运行时不依赖 `QApplication` 和 `QObject`。

### 10.3 下一步行动

1. **确认阶段一优先级**：是否立即开始基础设施替换？
2. **确认阶段三策略**：是否接受"框架去 Qt，插件可继续内部使用 Qt"的渐进方案？
3. **确认网络层 Qt 依赖**：是否允许我执行阶段二的"移除链接 + 编译验证"实验？
