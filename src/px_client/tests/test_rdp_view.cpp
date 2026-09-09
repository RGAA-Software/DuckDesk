#include "rdp/rdp_view.h"
#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <Windows.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace px::rdp {
namespace {
struct Inputs final {
    std::vector<std::pair<quint32, bool>> keys{};
    QString unicode{};
};

TEST(RdpView, NativeWindowMessagesPreservePhysicalScancodes) {
    auto view = std::make_unique<RdpView>();
    auto inputs = std::make_shared<Inputs>();
    QObject::connect(view.get(), &RdpView::KeyInput, view.get(), [inputs](quint32 code, bool down) { inputs->keys.emplace_back(code, down); });
    view->show();
    view->setFocus();
    QCoreApplication::processEvents();
    // Transient Win32 window ABI; Qt retains exclusive ownership of its native window.
    for (const auto key : {VK_LWIN, 0x52, 0x43, VK_TAB, VK_RETURN}) {
        const auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
        const auto bits = static_cast<LPARAM>(1u | ((scan & 255u) << 16) | ((scan & 0xff00) == 0xe000 ? 1u << 24 : 0));
        ASSERT_TRUE(PostMessageW(reinterpret_cast<HWND>(view->winId()), WM_KEYDOWN, key, bits));
        ASSERT_TRUE(PostMessageW(reinterpret_cast<HWND>(view->winId()), WM_KEYUP, key, bits | 0xc0000000u));
    }
    for (int pass{}; pass < 10; ++pass) {
        QCoreApplication::processEvents();
    }
    const std::vector<std::pair<quint32, bool>> expected{{0x15b, true}, {0x15b, false}, {0x13, true},  {0x13, false}, {0x2e, true},
                                                         {0x2e, false}, {0x0f, true},   {0x0f, false}, {0x1c, true},  {0x1c, false}};
    EXPECT_EQ(inputs->keys, expected);
}

TEST(RdpView, FocusLossReleasesHeldKeysAndImePreservesUnicode) {
    auto view = std::make_unique<RdpView>();
    auto inputs = std::make_shared<Inputs>();
    QObject::connect(view.get(), &RdpView::KeyInput, view.get(), [inputs](quint32 code, bool down) { inputs->keys.emplace_back(code, down); });
    QObject::connect(view.get(), &RdpView::UnicodeInput, view.get(), [inputs](quint16 code, bool down) {
        if (down) {
            inputs->unicode.append(QChar{code});
        }
    });
    QKeyEvent key{QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier, 0x1d, VK_CONTROL, 0};
    QApplication::sendEvent(view.get(), &key); // Transient synchronous Qt event boundary.
    view->ReleaseInput();
    view->ReleaseInput();
    const std::vector<std::pair<quint32, bool>> expected{{0x1d, true}, {0x1d, false}};
    EXPECT_EQ(inputs->keys, expected);
    QInputMethodEvent ime{};
    ime.setCommitString(QString::fromUtf8("中文输入🙂"));
    QApplication::sendEvent(view.get(), &ime);
    EXPECT_EQ(inputs->unicode, QString::fromUtf8("中文输入🙂"));
}
} // namespace
} // namespace px::rdp

int main(int argc, char** argv) { // NOLINT(gammaray-raw-pointer-boundary): CRT/Qt startup ABI, not retained by project state.
    ::testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};
    return RUN_ALL_TESTS();
}
