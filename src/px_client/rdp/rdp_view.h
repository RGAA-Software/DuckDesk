#pragma once

#include "rdp_frame.h"

#include <QCursor>
#include <QHash>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QSet>
#include <memory>

namespace px::rdp {

// Reuses D:/dolit/rdp's dirty-rectangle OpenGL rendering and input model, adapted
// to Qt 6 and single C++ ownership. This widget is parentless and smart-owned.
class RdpView final : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
    struct Graphics;

  public:
    RdpView();
    ~RdpView() override;
    void ApplyFrame(std::shared_ptr<const DesktopFrame> frame);
    void SetPointer(std::uint64_t id, const QImage& image, QPoint hotspot);
    void ActivatePointer(std::uint64_t id);
    void RemovePointer(std::uint64_t id);
    void SetDefaultPointer();
    void SetNullPointer();
    void ReleaseInput();
    void SendCtrlAltDel();
    [[nodiscard]] QSize ViewportPixels() const;

  signals:
    void MouseInput(quint16 flags, int x, int y, bool extended);
    void KeyInput(quint32 scancode, bool down);
    void UnicodeInput(quint16 codepoint, bool down);
    void SynchronizeInput(quint16 toggles);
    void PauseInput();
    void ViewportChanged(int width, int height);
    void FrameConsumed(quint64 frame_id);
    void FramePresented(quint64 frame_id, qint64 captured_us, qint64 presented_us);
    void DisplayFailed(QString reason);
    void DesktopRefreshRequested();

  protected:
    bool event(QEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary): synchronous Qt dispatch ABI.
    void initializeGL() override;
    void paintGL() override;
    // Qt dispatch ABI: these borrowed event values are used synchronously only.
    void resizeEvent(QResizeEvent* event) override;           // NOLINT(gammaray-raw-pointer-boundary)
    void mousePressEvent(QMouseEvent* event) override;        // NOLINT(gammaray-raw-pointer-boundary)
    void mouseReleaseEvent(QMouseEvent* event) override;      // NOLINT(gammaray-raw-pointer-boundary)
    void mouseMoveEvent(QMouseEvent* event) override;         // NOLINT(gammaray-raw-pointer-boundary)
    void wheelEvent(QWheelEvent* event) override;             // NOLINT(gammaray-raw-pointer-boundary)
    void keyPressEvent(QKeyEvent* event) override;            // NOLINT(gammaray-raw-pointer-boundary)
    void keyReleaseEvent(QKeyEvent* event) override;          // NOLINT(gammaray-raw-pointer-boundary)
    void focusInEvent(QFocusEvent* event) override;           // NOLINT(gammaray-raw-pointer-boundary)
    void focusOutEvent(QFocusEvent* event) override;          // NOLINT(gammaray-raw-pointer-boundary)
    void inputMethodEvent(QInputMethodEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary)

  private:
    void MouseButton(const QMouseEvent& event, bool down);
    void Key(const QKeyEvent& event, bool down);
    void UploadPending();
    void ConsumePending();
    std::shared_ptr<Graphics> graphics_{};
    std::shared_ptr<const DesktopFrame> pending_{};
    QSize desktop_{};
    QSet<quint32> pressed_keys_{};
    QHash<int, bool> pressed_mouse_{};
    QPoint last_mouse_{};
    QHash<quint64, QCursor> pointers_{};
    quint64 active_pointer_{0};
    int wheel_x_{0};
    int wheel_y_{0};
    bool uploaded_{false};
    bool shortcut_end_{false};
    bool shortcut_fullscreen_{false};
};

} // namespace px::rdp
