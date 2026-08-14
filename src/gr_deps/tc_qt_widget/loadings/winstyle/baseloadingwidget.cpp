#include "baseloadingwidget.h"
#include "tc_common_new/log.h"
#include <QApplication>
#include <QScreen>
#include <QMouseEvent>
namespace tc
{

    BaseLoadingWidget::BaseLoadingWidget(QWidget *parent)
            : QWidget(parent),
              m_animationTimer(new QTimer(this)),
              m_animationStop(false),
              m_duration(1000),
              m_extendDuration(150) {
        updateFrameRate();
    }

    void BaseLoadingWidget::setEasingCurve(const QEasingCurve &curve) {
        m_curve = curve;
        updateFrameValues();
    }

    void BaseLoadingWidget::setFrameRate(const int &frameRate) {
        m_frameRate = frameRate;
        updateFrameValues();
    }

    void BaseLoadingWidget::setDuration(const int &duration) {
        m_duration = duration;
        updateFrameValues();
    }

    void BaseLoadingWidget::setExtendDuration(const int &duration) {
        m_extendDuration = duration;
    }

    void BaseLoadingWidget::updateFrameData() {
        m_frameRects.clear();
        qreal refreshTime = 1000 / m_frameRate * 1.0;
        m_animationTimer->setInterval(refreshTime); // 刷新时间s
        int frameCount = m_duration / refreshTime;

        for (int i = 0; i < frameCount; ++i)
            m_frameRects.append(QRect());

        m_extendFrameCount = m_extendDuration / refreshTime;

        updateFrameRects();
    }

    void BaseLoadingWidget::start() {
        m_animationStop = false;
        disconnect(m_conn);
        m_conn = connect(m_animationTimer, &QTimer::timeout, this, &BaseLoadingWidget::updateFrame);
        if (!m_animationTimer->isActive()) {
            m_animationTimer->start();
        }
    }

    void BaseLoadingWidget::stop() {
        m_animationStop = true;
        disconnect(m_conn);
        m_animationTimer->stop();
    }

    void BaseLoadingWidget::updateFrameRate() {
        setFrameRate(/*qApp->primaryScreen()->refreshRate()*/60);
    }

    void BaseLoadingWidget::updateFrameValues() {
        m_frameValues.clear();
        int fCount = m_duration / refreshTime();
        qreal stepValue = 1.0 / fCount;
        LOGI("mDuration: {}, refreshTime: {}, fCount: {}, stepValue: {}", m_duration, refreshTime(), fCount, stepValue);
        for (int i = 0; i < fCount; ++i) {
            qreal progress = i * stepValue;
            m_frameValues.append(m_curve.valueForProgress(progress));
        }
    }

}