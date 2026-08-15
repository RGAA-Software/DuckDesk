//
// Created by RGAA on 2024-01-28.
//

#ifndef TC_CLIENT_PC_GL_FUNCTION_H
#define TC_CLIENT_PC_GL_FUNCTION_H

#if defined(WIN32) || defined(PC_PLATFORM)
#include <QOpenGLFunctions_3_3_Core>
#endif

#ifdef ANDROID
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
#include <GLES2/gl2ext.h>
#endif

#include "director.h"

namespace tc
{

    class GLFunctions {
    public:

#if defined(WIN32) || defined(PC_PLATFORM)
        QOpenGLFunctions_3_3_Core* core_ = nullptr;
#endif

    };

}

#endif //TC_CLIENT_PC_GL_FUNCTION_H
