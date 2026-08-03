#include <QApplication>
#include <QFontDatabase>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QCommandLineParser>
#include <QLockFile>
#include <QMessageBox>
#include <future>
#include <QStandardPaths>
#include "version_config.h"
#include "tc_common_new/log.h"
#include "tc_common_new/auto_start.h"
#include "tc_common_new/folder_util.h"
#include "render_panel/gr_application.h"
#include "render_panel/gr_workspace.h"
#include "render_panel/gr_running_pipe.h"
#include "render_panel/gr_settings.h"
#include "render_panel/util/opengl_helper.h"
#include "tc_common_new/win32/dxgi_mon_detector.h"
#include "tc_qt_widget/translator/tc_translator.h"
#include "tc_qt_widget/tc_font_manager.h"
#include "tc_common_new/shared_preference.h"
#include "tc_common_new/dump_helper.h"
#include "tc_common_new/hardware.h"
#include "tc_common_new/process_util.h"
#include "tc_common_new/time_util.h"
#include "tc_common_new/file_util.h"
#include "tc_common_new/folder_util.h"

using namespace tc;

std::shared_ptr<GrWorkspace> g_workspace = nullptr;

struct CommandLineOptions {
    bool run_automatically = false;
    bool debug = false;
    std::string skin_name;
};

CommandLineOptions ParseCommandLine(QApplication& app) {
    QCommandLineParser parser;
    parser.setApplicationDescription("GammaRay");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption run_automatically_option("run_automatically", "Run when logon.");
    parser.addOption(run_automatically_option);

    QCommandLineOption debug_option("debug", "Show startup debug message box and block until dismissed.");
    parser.addOption(debug_option);

    QCommandLineOption skin_option("skin", "Skin plugin name (e.g. skin_official, skin_opensource).", "name", "");
    parser.addOption(skin_option);
    parser.process(app);

    return CommandLineOptions {
        .run_automatically = parser.isSet(run_automatically_option),
        .debug = parser.isSet(debug_option),
        .skin_name = parser.value(skin_option).toStdString(),
    };
}

bool PrepareDirs(const QString& base_path) {
    std::vector<QString> dirs = {
        "gr_logs", "gr_data", "gr_data/client", "gr_data/render", "gr_data/panel",
        "gr_data/cache", "gr_dumps"
    };

    bool result = true;
    for (const QString& dir : dirs) {
        auto target_dir_path = base_path + "/" + dir;
        if (!QDir(target_dir_path).exists() && !QDir().mkpath(target_dir_path)) {
            LOGE("PrepareDirs failed, path: {}", target_dir_path.toStdString());
            result = false;
        }
    }
    return result;
}

int main(int argc, char *argv[]) {
    tc::Hardware::AcquirePermissionForRestartDevice();

    // run in high level
    tc::ProcessUtil::SetProcessInHighLevel();

    //::ChangeWindowMessageFilter(WM_DROPFILES, MSGFLT_ADD);
    //::ChangeWindowMessageFilter(WM_COPYDATA, MSGFLT_ADD);
    //::ChangeWindowMessageFilter(0x0049, MSGFLT_ADD);  // WM_COPYGLOBALDATA

    QApplication app(argc, argv);
    const auto options = ParseCommandLine(app);

    if (options.debug) {
        ::MessageBoxW(nullptr,
                      L"GammaRay main() reached",
                      L"GammaRay Startup Debug",
                      MB_OK | MB_TOPMOST | MB_SETFOREGROUND);
    }

    //CaptureDump();
    // Breakpad
    auto bc = BreakpadContext {
        .version_ = PROJECT_VERSION,
        .app_name_ = qApp->applicationName().toStdString(),
    };
    
    auto _r = std::async([]() {
        ClearOldDumps();
    });
    CaptureDumpByBreakpad(&bc);

    //auto base_dir = QApplication::applicationDirPath();
    auto base_dir = QString::fromStdWString(FolderUtil::GetProgramDataPath());
    if (!PrepareDirs(base_dir)) {
        QMessageBox::critical(nullptr, "Startup failed", "ProgramData directory initialization failed. Please check permissions.");
        return -1;
    }

    auto log_path = base_dir + "/gr_logs/godesk.log";
    Logger::InitLog(log_path.toStdWString(), true);

    // Check OpenGL Backend
    EOpenGLBackend backend = DetectOpenGLBackend();
    LOGI("Detected backend:", (int)backend);
    QString gl_backend = "angle";
    // 设置 QT_OPENGL 环境变量
    switch (backend) {
        case EOpenGLBackend::kDesktop: {
            QString backend = "desktop";
            gl_backend = backend;
            break;
        }
        case EOpenGLBackend::kGLES: {
            QString backend = "angle";
            gl_backend = backend;
            break;
        }
        case EOpenGLBackend::kSoftware: {
            QString backend = "software";
            gl_backend = backend;
            break;
        }
        default: {
            QString backend = "angle";
            gl_backend = backend;
            break;
        }
    }
    GrSettings::Instance()->gl_backend_ = gl_backend.toStdString();
    LOGI("gl_backed:{}", gl_backend.toStdString());

    LOGI("Commands:");
    LOGI("  Run automatically: {}", options.run_automatically);
    LOGI("  Debug: {}", options.debug);
    LOGI("  Skin: {}", options.skin_name);

    // pipe — 单实例:已有实例在监听时,SendHello 会成功并通知其前置显示,
    // 本实例直接退出(不再继续启动,避免第二实例抢 SharedPreference 的 LOCK
    // 弹 "Startup failed" 对话框)。
    auto rn_pipe = std::make_shared<GrRunningPipe>();
    if (rn_pipe->SendHello()) {
        LOGI("Another panel instance is running, notified it to foreground, exit this one.");
        return 0;
    }
    rn_pipe->StartListening([=]() {
        LOGI("received foreground request from another instance, g_workspace={}", (void*)g_workspace.get());
        if (!g_workspace) {
            return;
        }
        // 管道回调在工作线程,Qt 窗口操作必须投递到 UI 线程;
        // 且 Windows 有前台锁定,raise 单独无效,需要激活 + 前台 hack。
        QMetaObject::invokeMethod(g_workspace.get(), [=]() {
#ifdef WIN32
            auto hwnd = (HWND)g_workspace->winId();
            // 最小化/隐藏都要先恢复
            ShowWindow(hwnd, SW_RESTORE);
#endif
            g_workspace->setWindowState((g_workspace->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
            g_workspace->show();
            g_workspace->raise();
            g_workspace->activateWindow();
#ifdef WIN32
            auto fore = GetForegroundWindow();
            if (fore) {
                auto fore_tid = GetWindowThreadProcessId(fore, nullptr);
                auto cur_tid = GetCurrentThreadId();
                AttachThreadInput(cur_tid, fore_tid, TRUE);
                SetForegroundWindow(hwnd);
                BringWindowToTop(hwnd);
                AttachThreadInput(cur_tid, fore_tid, FALSE);
            } else {
                SetForegroundWindow(hwnd);
                BringWindowToTop(hwnd);
            }
#endif
            LOGI("foreground request handled, minimized={}, hidden={}", g_workspace->isMinimized(), g_workspace->isHidden());
        }, Qt::QueuedConnection);
    });

    // init sp
    auto data_dir = base_dir + "/gr_data";
    if (!SharedPreference::Instance()->Init(data_dir.toStdWString(), "godesk.dat")) {
        auto err = QString::fromStdString(SharedPreference::Instance()->GetLastError());
        QMessageBox::critical(nullptr, "Startup failed", "SharedPreference init failed:\n" + err);
        return -1;
    }

    GrSettings::Instance()->gr_data_path_ = data_dir.toStdString();

    {
        auto auto_start = std::make_shared<tc::AutoStart>();
        auto path = QApplication::applicationFilePath().toStdString();
        auto_start->NewLogonTask((char*)"GammaRay_Panel_Start", (char*)path.c_str(), (char*)"--run_automatically", (char*)"GR");
    }

    auto mon_detector = DxgiMonitorDetector::Instance();
    mon_detector->DetectAdapters();
    mon_detector->PrintAdapters();

    tcFontMgr()->InitFont(":/resources/font/ms_yahei.ttf");

    // init language
    tcTrMgr()->InitLanguage();

    g_workspace = std::make_shared<GrWorkspace>(options.run_automatically, options.skin_name);
    g_workspace->Init();
    g_workspace->setFixedSize(1450, 800);
    if (!options.run_automatically) {
        g_workspace->show();
    }

    return app.exec();
}
