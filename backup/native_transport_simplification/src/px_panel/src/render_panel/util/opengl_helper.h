#pragma once

#include <QApplication>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QDebug>
#include <QOperatingSystemVersion>

namespace px
{

    enum class EOpenGLBackend {
        kDesktop,
        kGLES,
        kSoftware,
        kUnknown
    };

    EOpenGLBackend DetectOpenGLBackend();
}