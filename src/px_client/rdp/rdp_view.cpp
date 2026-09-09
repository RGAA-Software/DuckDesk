#include "rdp_view.h"

#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QPointer>
#include <QResizeEvent>
#include <QWheelEvent>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>

namespace px::rdp {
namespace {

qint64 MonotonicUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// MS-RDPBCGR input wire values; the Qt view does not depend on a FreeRDP ABI.
quint16 MouseFlag(Qt::MouseButton button) {
    switch (button) {
    case Qt::LeftButton:
        return 0x1000;
    case Qt::RightButton:
        return 0x2000;
    case Qt::MiddleButton:
        return 0x4000;
    case Qt::BackButton:
        return 1;
    case Qt::ForwardButton:
        return 2;
    default:
        return 0;
    }
}

quint32 ScanCode(const QKeyEvent& event) {
    const auto native = event.nativeScanCode();
    if (native != 0) {
        // Qt 6 Windows puts E0 in nativeModifiers bit 24, not scan-code bit 8.
        // Losing it turns Win/right-Ctrl/navigation keys into different keys.
        auto code = (native & 0x1ff) | ((event.nativeModifiers() & 0x01000000u) != 0 ? 0x100u : 0u);
        if (code == 0x145 || code == 0x136) {
            code &= 0xff;
        } // RDP NumLock/right-Shift normalization.
        return code;
    }
    const auto mapped = MapVirtualKeyW(event.nativeVirtualKey(), MAPVK_VK_TO_VSC_EX);
    return (mapped & 0xff) | ((mapped & 0xff00) == 0xe000 ? 0x100 : 0);
}

} // namespace

struct RdpView::Graphics final {
    std::unique_ptr<QOpenGLShaderProgram> program{};
    std::unique_ptr<QOpenGLTexture> texture{};
    QSize size{};
    void Reset() {
        texture.reset();
        program.reset();
        size = {};
    }
};

RdpView::RdpView() : QOpenGLWidget(nullptr), graphics_(std::make_shared<Graphics>()) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 200);
    setMouseTracking(true);
    setAttribute(Qt::WA_InputMethodEnabled, true);
}

RdpView::~RdpView() {
    makeCurrent();
    graphics_->Reset();
    doneCurrent();
}

void RdpView::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0, 0, 0, 1);
    graphics_->Reset();
    // Parentless shader program has exactly one C++ owner.
    graphics_->program = std::make_unique<QOpenGLShaderProgram>();
    constexpr char vertex[] = "attribute vec2 pos; attribute vec2 tex; varying vec2 uv;"
                              "void main() { uv = tex; gl_Position = vec4(pos, 0.0, 1.0); }";
    constexpr char fragment[] = "varying vec2 uv; uniform sampler2D image;"
                                "void main() { gl_FragColor = texture2D(image, uv); }";
    if (!graphics_->program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) ||
        !graphics_->program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) || !graphics_->program->link()) {
        graphics_->Reset();
        emit DisplayFailed(QStringLiteral("RDP OpenGL shader initialization failed"));
        return;
    }
    const auto view = QPointer<RdpView>{this}; // NOLINT(gammaray-raw-pointer-boundary): Qt observation, not ownership.
    QObject::connect(
        context(), &QOpenGLContext::aboutToBeDestroyed, context(),
        [view, weak = std::weak_ptr<Graphics>{graphics_}] {
            if (const auto graphics = weak.lock(); graphics && view) {
                view->makeCurrent();
                graphics->Reset();
                view->uploaded_ = false;
                view->doneCurrent();
            }
        },
        Qt::DirectConnection);
    emit DesktopRefreshRequested();
}

void RdpView::ApplyFrame(std::shared_ptr<const DesktopFrame> frame) {
    if (!frame || !frame->IsValid()) {
        emit DisplayFailed(QStringLiteral("RDP invalid frame metadata"));
        return;
    }
    if (pending_) {
        emit DisplayFailed(QStringLiteral("RDP frame backpressure violation: pending=%1 incoming=%2").arg(pending_->frame_id).arg(frame->frame_id));
        return;
    }
    desktop_ = frame->desktop;
    pending_ = std::move(frame);
    uploaded_ = false;
    // Hidden/minimized windows still apply incremental patches before acknowledging;
    // they must never accumulate an unbounded GUI queue or lose dirty rectangles.
    if (isValid() && !isVisible()) {
        makeCurrent();
        UploadPending();
        doneCurrent();
        if (uploaded_) {
            ConsumePending();
        }
    }
    update();
}

void RdpView::UploadPending() {
    if (!pending_ || uploaded_ || !graphics_->program) {
        return;
    }
    if (!graphics_->texture || graphics_->size != pending_->desktop) {
        auto texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
        texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
        texture->setSize(pending_->desktop.width(), pending_->desktop.height());
        texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
        texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        texture->allocateStorage(QOpenGLTexture::BGRA, QOpenGLTexture::UInt8);
        if (!texture->isStorageAllocated()) {
            emit DisplayFailed(QStringLiteral("RDP texture allocation failed"));
            return;
        }
        // A fresh/resized desktop requires a full image. Never show uninitialized GPU pixels.
        const QByteArray black(pending_->desktop.width() * pending_->desktop.height() * 4, '\0');
        texture->setData(QOpenGLTexture::BGRA, QOpenGLTexture::UInt8, black.constData());
        graphics_->size = pending_->desktop;
        graphics_->texture = std::move(texture);
    }
    graphics_->texture->bind();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    qsizetype offset{};
    for (const auto& rectangle : pending_->rectangles) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, rectangle.x(), rectangle.y(), rectangle.width(), rectangle.height(), GL_BGRA, GL_UNSIGNED_BYTE,
                        pending_->pixels.constData() + offset); // Transient OpenGL upload ABI.
        offset += static_cast<qsizetype>(rectangle.width()) * rectangle.height() * 4;
    }
    graphics_->texture->release();
    uploaded_ = true;
}

void RdpView::ConsumePending() {
    const auto frame = std::exchange(pending_, {});
    if (frame) {
        emit FrameConsumed(frame->frame_id);
    }
}

void RdpView::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);
    UploadPending();
    if (!graphics_->program || !graphics_->texture || width() <= 0 || height() <= 0) {
        return;
    }
    const auto target = DesktopViewport(graphics_->size, size());
    const float x0 = static_cast<float>(target.x()) / width() * 2 - 1;
    const float x1 = static_cast<float>(target.x() + target.width()) / width() * 2 - 1;
    const float y0 = 1 - static_cast<float>(target.y()) / height() * 2;
    const float y1 = 1 - static_cast<float>(target.y() + target.height()) / height() * 2;
    const std::array<GLfloat, 8> positions{x0, y0, x0, y1, x1, y0, x1, y1};
    constexpr std::array<GLfloat, 8> uv{0, 0, 0, 1, 1, 0, 1, 1};
    graphics_->program->bind();
    graphics_->texture->bind(0);
    graphics_->program->setUniformValue("image", 0);
    graphics_->program->enableAttributeArray("pos");
    graphics_->program->enableAttributeArray("tex");
    graphics_->program->setAttributeArray("pos", positions.data(), 2);
    graphics_->program->setAttributeArray("tex", uv.data(), 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    graphics_->program->disableAttributeArray("pos");
    graphics_->program->disableAttributeArray("tex");
    graphics_->program->release();
    graphics_->texture->release();
    if (pending_ && uploaded_) {
        emit FramePresented(pending_->frame_id, pending_->captured_us, MonotonicUs());
        ConsumePending();
    }
}

QSize RdpView::ViewportPixels() const {
    return {qRound(width() * devicePixelRatioF()), qRound(height() * devicePixelRatioF())};
}

void RdpView::SetPointer(std::uint64_t id, const QImage& image, QPoint hotspot) {
    if (image.isNull() || image.width() > 512 || image.height() > 512 || hotspot.x() < 0 || hotspot.y() < 0 || hotspot.x() >= image.width() ||
        hotspot.y() >= image.height() || (!pointers_.contains(id) && pointers_.size() >= 1024)) {
        return;
    }
    pointers_.insert(id, QCursor{QPixmap::fromImage(image), hotspot.x(), hotspot.y()});
    if (active_pointer_ == id) {
        ActivatePointer(id);
    }
}
void RdpView::ActivatePointer(std::uint64_t id) {
    active_pointer_ = id;
    if (pointers_.contains(id)) {
        setCursor(pointers_.value(id));
    }
}
void RdpView::RemovePointer(std::uint64_t id) {
    pointers_.remove(id);
    if (active_pointer_ == id) {
        SetDefaultPointer();
    }
}
void RdpView::SetDefaultPointer() {
    active_pointer_ = 0;
    setCursor(Qt::ArrowCursor);
}
void RdpView::SetNullPointer() {
    active_pointer_ = 0;
    setCursor(Qt::BlankCursor);
}

void RdpView::resizeEvent(QResizeEvent* event) { // NOLINT(gammaray-raw-pointer-boundary): Qt dispatch ABI.
    QOpenGLWidget::resizeEvent(event);
    const auto pixels = ViewportPixels();
    emit ViewportChanged(pixels.width(), pixels.height());
}

void RdpView::MouseButton(const QMouseEvent& event, bool down) {
    const auto flags = MouseFlag(event.button());
    const bool extended = event.button() == Qt::BackButton || event.button() == Qt::ForwardButton;
    const auto point = MapDesktopPoint(desktop_, size(), event.position().toPoint(), !down);
    if (!point || flags == 0 || (!down && !pressed_mouse_.contains(static_cast<int>(event.button())))) {
        return;
    }
    if (down) {
        pressed_mouse_.insert(static_cast<int>(event.button()), extended);
    } else {
        pressed_mouse_.remove(static_cast<int>(event.button()));
    }
    last_mouse_ = *point;
    emit MouseInput(static_cast<quint16>(flags | (down ? 0x8000 : 0)), point->x(), point->y(), extended);
}

void RdpView::mousePressEvent(QMouseEvent* event) { // NOLINT(gammaray-raw-pointer-boundary): Qt dispatch ABI.
    setFocus();
    MouseButton(*event, true);
    event->accept();
}
void RdpView::mouseReleaseEvent(QMouseEvent* event) { // NOLINT(gammaray-raw-pointer-boundary): Qt dispatch ABI.
    MouseButton(*event, false);
    event->accept();
}
void RdpView::mouseMoveEvent(QMouseEvent* event) { // NOLINT(gammaray-raw-pointer-boundary): Qt dispatch ABI.
    if (const auto point = MapDesktopPoint(desktop_, size(), event->position().toPoint(), !pressed_mouse_.isEmpty())) {
        last_mouse_ = *point;
        emit MouseInput(0x0800, point->x(), point->y(), false);
    }
    event->accept();
}
void RdpView::wheelEvent(QWheelEvent* event) { // NOLINT(gammaray-raw-pointer-boundary): Qt dispatch ABI.
    const auto point = MapDesktopPoint(desktop_, size(), event->position().toPoint());
    if (!point) {
        return;
    }
    wheel_x_ += std::clamp(event->angleDelta().x(), -12000, 12000);
    wheel_y_ += std::clamp(event->angleDelta().y(), -12000, 12000);
    for (unsigned int axis{}; axis < 2; ++axis) {
        auto& accumulated = axis == 0 ? wheel_y_ : wheel_x_;
        while (accumulated >= 120 || accumulated <= -120) {
            const int step = accumulated > 0 ? 120 : -120;
            emit MouseInput(static_cast<quint16>((axis == 0 ? 0x0200 : 0x0400) | (step & 0x01ff)), point->x(), point->y(), false);
            accumulated -= step;
        }
    }
    event->accept();
}

void RdpView::SendCtrlAltDel() {
    for (const quint32 code : {0x1du, 0x38u, 0x153u}) {
        emit KeyInput(code, true);
    }
    for (const quint32 code : {0x153u, 0x38u, 0x1du}) {
        emit KeyInput(code, false);
    }
    // Restore modifiers which remain physically held after the synthesized chord.
    for (const quint32 code : pressed_keys_) {
        emit KeyInput(code, true);
    }
}

void RdpView::Key(const QKeyEvent& event, bool down) {
    if (!down && event.isAutoRepeat()) {
        return;
    }
    if (event.key() == Qt::Key_Return || event.key() == Qt::Key_Enter) {
        if (down && event.modifiers().testFlag(Qt::ControlModifier) && event.modifiers().testFlag(Qt::AltModifier)) {
            if (!event.isAutoRepeat()) {
                ReleaseInput();
                shortcut_fullscreen_ = true;
                if (isFullScreen()) {
                    showNormal();
                } else {
                    showFullScreen();
                }
            }
            return;
        }
        if (!down && std::exchange(shortcut_fullscreen_, false)) {
            return;
        }
    }
    if (event.key() == Qt::Key_End) {
        if (down && event.modifiers().testFlag(Qt::ControlModifier) && event.modifiers().testFlag(Qt::AltModifier)) {
            shortcut_end_ = true;
            if (!event.isAutoRepeat()) {
                SendCtrlAltDel();
            }
            return;
        }
        if (!down && std::exchange(shortcut_end_, false)) {
            return;
        }
    }
    if (event.key() == Qt::Key_Pause || event.nativeVirtualKey() == VK_PAUSE) {
        if (down && !event.isAutoRepeat()) {
            emit PauseInput();
        }
        return;
    }
    const auto code = ScanCode(event);
    if (code != 0) {
        if (down) {
            pressed_keys_.insert(code);
        } else {
            pressed_keys_.remove(code);
        }
        emit KeyInput(code, down);
    } else if (down) {
        for (const QChar character : event.text()) {
            if (character.unicode() >= 0x20 && character.unicode() != 0x7f) {
                emit UnicodeInput(character.unicode(), true);
                emit UnicodeInput(character.unicode(), false);
            }
        }
    }
}

void RdpView::keyPressEvent(QKeyEvent* event) { // NOLINT(gammaray-raw-pointer-boundary): Qt ABI.
    Key(*event, true);
    event->accept();
} // NOLINT(gammaray-raw-pointer-boundary): Qt ABI.
void RdpView::keyReleaseEvent(QKeyEvent* event) { // NOLINT(gammaray-raw-pointer-boundary): Qt ABI.
    Key(*event, false);
    event->accept();
} // NOLINT(gammaray-raw-pointer-boundary): Qt ABI.

bool RdpView::event(QEvent* event) { // NOLINT(gammaray-raw-pointer-boundary): synchronous Qt dispatch ABI.
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        const auto& key = static_cast<const QKeyEvent&>(*event);
        if (key.key() == Qt::Key_Tab || key.key() == Qt::Key_Backtab) {
            Key(key, event->type() == QEvent::KeyPress);
            event->accept();
            return true; // QWidget otherwise consumes Tab as local focus traversal.
        }
    }
    return QOpenGLWidget::event(event);
}

void RdpView::ReleaseInput() {
    for (const auto code : std::exchange(pressed_keys_, {})) {
        emit KeyInput(code, false);
    }
    const auto buttons = std::exchange(pressed_mouse_, {});
    for (auto iterator = buttons.cbegin(); iterator != buttons.cend(); ++iterator) {
        emit MouseInput(MouseFlag(static_cast<Qt::MouseButton>(iterator.key())), last_mouse_.x(), last_mouse_.y(), iterator.value());
    }
    wheel_x_ = 0;
    wheel_y_ = 0;
    shortcut_end_ = false;
    shortcut_fullscreen_ = false;
}

void RdpView::focusInEvent(QFocusEvent* event) { // NOLINT(gammaray-raw-pointer-boundary): Qt dispatch ABI.
    QOpenGLWidget::focusInEvent(event);
    const auto toggles = static_cast<quint16>(((GetKeyState(VK_SCROLL) & 1) ? 1 : 0) | ((GetKeyState(VK_NUMLOCK) & 1) ? 2 : 0) |
                                              ((GetKeyState(VK_CAPITAL) & 1) ? 4 : 0) | ((GetKeyState(VK_KANA) & 1) ? 8 : 0));
    emit SynchronizeInput(toggles);
}
void RdpView::focusOutEvent(QFocusEvent* event) { // NOLINT(gammaray-raw-pointer-boundary): Qt dispatch ABI.
    ReleaseInput();
    QOpenGLWidget::focusOutEvent(event);
}
void RdpView::inputMethodEvent(QInputMethodEvent* event) { // NOLINT(gammaray-raw-pointer-boundary): Qt dispatch ABI.
    for (const QChar character : event->commitString()) {
        emit UnicodeInput(character.unicode(), true);
        emit UnicodeInput(character.unicode(), false);
    }
    event->accept();
}

} // namespace px::rdp
