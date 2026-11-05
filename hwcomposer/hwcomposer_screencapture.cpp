/****************************************************************************
**
** Copyright (C) 2026 Jolla Ltd.
**
** This file is part of the hwcomposer plugin.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "hwcomposer_screencapture.h"

#include <hwcomposer_window.h>
#include <sync/sync.h>

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QMetaObject>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <qdebug.h>

#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>

extern "C" void *android_dlopen(const char *filename, int flags);
extern "C" void *android_dlsym(void *handle, const char *symbol);

#ifndef EGL_RECORDABLE_ANDROID
#define EGL_RECORDABLE_ANDROID 0x3142
#endif

// Matches sailfish::ScreenCaptureSession in droidmedia/screen_capture_service.h.
struct MinisfScreenCaptureSessionInfo {
    uint64_t generation;
    int32_t width;
    int32_t height;
    int32_t fps;
    int32_t target;
    int32_t contentLeft;
    int32_t contentTop;
    int32_t contentRight;
    int32_t contentBottom;
};

enum { TargetDisplay = 0 };
enum { SessionChanged = 1, FrameRequested = 2 };

static const int MaxSessionSize = 4096;

// A session that keeps the compositor waiting longer than this is given up.
static const int FenceTimeoutMs = 250;
static const int64_t MaxSwapNs = 250000000;

// Clip space, -1..+1 is the whole viewport
static const GLfloat Positions[8] = { -1, -1,  1, -1,  -1, 1,  1, 1 };

// Texture space (0..1) coordinates of the same corners for rotation 0, 90 and 270.
// They all flip-y.
static const GLfloat TexCoords[3][8] = {
    { 0, 1,  1, 1,  0, 0,  1, 0 },
    { 1, 1,  1, 0,  0, 1,  0, 0 },
    { 0, 0,  0, 1,  1, 0,  1, 1 },
};

static int64_t monotonicNs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

static bool validSessionSize(int size)
{
    return size > 0 && size <= MaxSessionSize && size % 2 == 0;
}

static GLuint compileShader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512] = "";
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        qWarning("screencap: shader compilation failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint createProgram()
{
    static const char *vertexSource = R"(
        attribute vec2 aPosition;
        attribute vec2 aTexCoord;
        varying vec2 vTexCoord;
        void main() {
            gl_Position = vec4(aPosition, 0.0, 1.0);
            vTexCoord = aTexCoord;
        })";
    static const char *fragmentSource = R"(
        precision mediump float;
        uniform sampler2D uTexture;
        varying vec2 vTexCoord;
        void main() {
            gl_FragColor = texture2D(uTexture, vTexCoord);
        })";

    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    GLuint program = 0;
    if (vertex && fragment) {
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glBindAttribLocation(program, 0, "aPosition");
        glBindAttribLocation(program, 1, "aTexCoord");
        glLinkProgram(program);

        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            qWarning("screencap: program link failed");
            glDeleteProgram(program);
            program = 0;
        }
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

namespace {

// Makes the encoder Surface current and restores the compositor's context
// when it goes out of scope.
class ScopedCaptureContext
{
public:
    ScopedCaptureContext(EGLDisplay display, EGLSurface surface, EGLContext context)
        : m_eglDisplay(display)
        , m_savedDraw(eglGetCurrentSurface(EGL_DRAW))
        , m_savedRead(eglGetCurrentSurface(EGL_READ))
        , m_savedContext(eglGetCurrentContext())
        , m_current(eglMakeCurrent(display, surface, surface, context))
    {
    }

    ~ScopedCaptureContext()
    {
        if (!eglMakeCurrent(m_eglDisplay, m_savedDraw, m_savedRead, m_savedContext))
            qWarning("screencap: cannot restore the compositor's EGL context (0x%x)", eglGetError());
    }

    bool isCurrent() const { return m_current; }

private:
    EGLDisplay m_eglDisplay;
    EGLSurface m_savedDraw;
    EGLSurface m_savedRead;
    EGLContext m_savedContext;
    bool m_current;
};

struct ScopedFd {
    ~ScopedFd() { if (fd >= 0) close(fd); }
    int fd;
};

}

HwComposerScreenCapture *HwComposerScreenCapture::create(int displayWidth, int displayHeight)
{
    const QByteArray enabled = qgetenv("QPA_HWC_SCREENCAP");
    if (enabled != "1" && enabled != "true")
        return nullptr;

    int landscapeRotation = 270;
    const QByteArray rotation = qgetenv("QPA_HWC_SCREENCAP_LANDSCAPE_ROTATION");
    if (rotation == "90")
        landscapeRotation = 90;
    else if (!rotation.isEmpty() && rotation != "270")
        qWarning("screencap: QPA_HWC_SCREENCAP_LANDSCAPE_ROTATION must be 90 or 270, using 270");

    // Never closed: libminisf runs Binder threads in this process.
    void *libminisf = android_dlopen("libminisf.so", RTLD_LAZY);
    if (!libminisf) {
        qWarning("screencap: libminisf.so is not available, capture disabled");
        return nullptr;
    }

    HwComposerScreenCapture *capture =
        new HwComposerScreenCapture(displayWidth, displayHeight, landscapeRotation);
    if (!capture->resolve(libminisf)) {
        delete capture;
        return nullptr;
    }

    capture->m_setCallback(sessionCallback, capture);

    qDebug("screencap: enabled for a %dx%d display, landscape rotation %d",
           displayWidth, displayHeight, landscapeRotation);
    return capture;
}

HwComposerScreenCapture::HwComposerScreenCapture(int displayWidth, int displayHeight,
                                                 int landscapeRotation)
    : m_displayWidth(displayWidth)
    , m_displayHeight(displayHeight)
    , m_landscapeRotation(landscapeRotation)
{
}

HwComposerScreenCapture::~HwComposerScreenCapture()
{
    // Waits for a running callback, which posts events to this object.
    if (m_setCallback)
        m_setCallback(nullptr, nullptr);

    detach();
    if (m_fenceFd >= 0)
        close(m_fenceFd);
}

bool HwComposerScreenCapture::resolve(void *libminisf)
{
    m_sessionQuery = reinterpret_cast<decltype(m_sessionQuery)>(
        android_dlsym(libminisf, "minisf_screen_capture_session_query"));
    m_targetAcquireGeneration = reinterpret_cast<decltype(m_targetAcquireGeneration)>(
        android_dlsym(libminisf, "minisf_screen_capture_target_acquire_generation"));
    m_targetNativeWindow = reinterpret_cast<decltype(m_targetNativeWindow)>(
        android_dlsym(libminisf, "minisf_screen_capture_target_native_window"));
    m_targetRelease = reinterpret_cast<decltype(m_targetRelease)>(
        android_dlsym(libminisf, "minisf_screen_capture_target_release"));
    m_setCallback = reinterpret_cast<decltype(m_setCallback)>(
        android_dlsym(libminisf, "minisf_screen_capture_set_callback"));
    if (!m_sessionQuery || !m_targetAcquireGeneration || !m_targetNativeWindow ||
        !m_targetRelease || !m_setCallback) {
        qWarning("screencap: libminisf has no screen capture API, capture disabled");
        return false;
    }

    // Through eglGetProcAddress, so that libhybris can map its buffers for
    // the EGLImage calls.
    m_eglCreateImageKHR = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        eglGetProcAddress("eglCreateImageKHR"));
    m_eglDestroyImageKHR = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
        eglGetProcAddress("eglDestroyImageKHR"));
    m_glEGLImageTargetTexture2DOES = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    m_eglPresentationTimeANDROID = reinterpret_cast<PFNEGLPRESENTATIONTIMEANDROIDPROC>(
        eglGetProcAddress("eglPresentationTimeANDROID"));
    if (!m_eglCreateImageKHR || !m_eglDestroyImageKHR || !m_glEGLImageTargetTexture2DOES || !m_eglPresentationTimeANDROID) {
        qWarning("screencap: EGLImage or presentation time extensions are missing, "
                 "capture disabled");
        return false;
    }

    return true;
}

// On a Binder thread. The repaint's frame attaches to a new session, detaches from
// an ended one, or is the frame a recorder asked for.
void HwComposerScreenCapture::sessionCallback(void *user, int event)
{
    HwComposerScreenCapture *capture = static_cast<HwComposerScreenCapture *>(user);

    if (event == FrameRequested) {
        MinisfScreenCaptureSessionInfo info;
        if (!capture->m_sessionQuery(&info) || info.target != TargetDisplay)
            return;
    } else if (event != SessionChanged) {
        return;
    }

    QCoreApplication::postEvent(capture, new QEvent(QEvent::User));
}

bool HwComposerScreenCapture::event(QEvent *e)
{
    if (e->type() != QEvent::User)
        return QObject::event(e);

    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow *window : windows) {
        if (!window->isExposed())
            continue;
        // QQuickWindow::update() renders and swaps even an unchanged scene;
        // requestUpdate() does not with the threaded render loop.
        if (window->metaObject()->indexOfSlot("update()") >= 0)
            QMetaObject::invokeMethod(window, "update");
        else
            window->requestUpdate();
    }
    return true;
}

void HwComposerScreenCapture::bufferPresented(HWComposerNativeWindowBuffer *buffer,
                                              int acquireFenceFd)
{
    if (m_fenceFd >= 0)
        close(m_fenceFd);
    m_fenceFd = -1;
    m_buffer = buffer;

    // The display takes the fence; keep a copy to wait on before reading.
    if (acquireFenceFd >= 0) {
        m_fenceFd = dup(acquireFenceFd);
        if (m_fenceFd < 0) {
            qWarning("screencap: cannot duplicate the acquire fence (errno %d)", errno);
            m_buffer = nullptr;
        }
    }
}

void HwComposerScreenCapture::captureFrame()
{
    HWComposerNativeWindowBuffer *buffer = m_buffer;
    ScopedFd fence = { m_fenceFd };
    m_buffer = nullptr;
    m_fenceFd = -1;

    if (!buffer)
        return;

    checkSession();
    if (!m_target)
        return;

    if (fence.fd >= 0 && sync_wait(fence.fd, FenceTimeoutMs) != 0) {
        qWarning("screencap: the display buffer was not ready in %d ms", FenceTimeoutMs);
        fail();
        return;
    }

    if (!submit(buffer))
        fail();
}

void HwComposerScreenCapture::checkSession()
{
    MinisfScreenCaptureSessionInfo info;
    if (!m_sessionQuery(&info)) {
        if (m_target) {
            qDebug("screencap: session %" PRIu64 " ended, detaching", m_generation);
            detach();
        }
        return;
    }

    if (m_target) {
        if (info.generation == m_generation)
            return;
        qDebug("screencap: session %" PRIu64 " replaced, detaching", m_generation);
        detach();
    }
    if (info.generation == m_ignoredGeneration)
        return;

    if (info.target != TargetDisplay) {
        qDebug("screencap: ignoring session %" PRIu64 ": target %d is not the display",
               info.generation, info.target);
        m_ignoredGeneration = info.generation;
        return;
    }

    if (!validSessionSize(info.width) || !validSessionSize(info.height) || info.fps <= 0) {
        qWarning("screencap: rejecting session %" PRIu64 " of %dx%d@%d: sizes must be even "
                 "and at most %d", info.generation, info.width, info.height, info.fps,
                 MaxSessionSize);
        m_ignoredGeneration = info.generation;
        return;
    }

    const QRect content(info.contentLeft, info.contentTop, info.contentRight - info.contentLeft,
                        info.contentBottom - info.contentTop);
    attach(info, captureFit(QSize(m_displayWidth, m_displayHeight), QSize(info.width, info.height),
                            content, m_landscapeRotation));
}

void HwComposerScreenCapture::attach(const MinisfScreenCaptureSessionInfo &info,
                                     const CaptureFit &fit)
{
    // Null if another recorder took over since the query; the next frame sees it.
    m_target = m_targetAcquireGeneration(info.generation);
    if (!m_target)
        return;
    m_generation = info.generation;
    m_eglDisplay = eglGetCurrentDisplay();

    static const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RECORDABLE_ANDROID, EGL_TRUE,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    static const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

    EGLConfig config;
    EGLint configs = 0;
    if (!eglChooseConfig(m_eglDisplay, configAttribs, &config, 1, &configs) || configs == 0) {
        qWarning("screencap: no recordable EGL config (0x%x)", eglGetError());
        fail();
        return;
    }

    // A private context, so that no GL state is shared with the compositor.
    m_context = eglCreateContext(m_eglDisplay, config, EGL_NO_CONTEXT, contextAttribs);
    if (m_context == EGL_NO_CONTEXT) {
        qWarning("screencap: eglCreateContext failed (0x%x)", eglGetError());
        fail();
        return;
    }

    m_encoderSurface = eglCreateWindowSurface(m_eglDisplay, config,
        reinterpret_cast<EGLNativeWindowType>(m_targetNativeWindow(m_target)), nullptr);
    if (m_encoderSurface == EGL_NO_SURFACE) {
        qWarning("screencap: eglCreateWindowSurface failed (0x%x)", eglGetError());
        fail();
        return;
    }

    // Swap interval 0 puts the Surface in async mode, so the compositor never
    // waits for the encoder to release a buffer.
    bool ready;
    {
        ScopedCaptureContext scope(m_eglDisplay, m_encoderSurface, m_context);
        ready = scope.isCurrent() && eglSwapInterval(m_eglDisplay, 0);
        if (!ready)
            qWarning("screencap: cannot set up the encoder Surface (0x%x)", eglGetError());
    }
    if (!ready) {
        fail();
        return;
    }

    m_width = info.width;
    m_height = info.height;
    m_fit = fit;
    const QRect &vp = m_fit.viewport;
    qDebug("screencap: attached to session %" PRIu64 ": %dx%d@%d, rotation %d, "
           "viewport %d,%d %dx%d", m_generation, m_width, m_height, info.fps,
           m_fit.rotation, vp.x(), vp.y(), vp.width(), vp.height());
}

void HwComposerScreenCapture::detach()
{
    // Destroying the private context also deletes its program and textures.
    if (m_context != EGL_NO_CONTEXT)
        eglDestroyContext(m_eglDisplay, m_context);
    for (auto &source : m_sources)
        m_eglDestroyImageKHR(m_eglDisplay, source.second.image);
    if (m_encoderSurface != EGL_NO_SURFACE)
        eglDestroySurface(m_eglDisplay, m_encoderSurface);
    if (m_target)
        m_targetRelease(m_target);

    m_sources.clear();
    m_program = 0;
    m_context = EGL_NO_CONTEXT;
    m_encoderSurface = EGL_NO_SURFACE;
    m_eglDisplay = EGL_NO_DISPLAY;
    m_target = nullptr;
    m_generation = 0;
}

void HwComposerScreenCapture::fail()
{
    qWarning("screencap: giving up on session %" PRIu64, m_generation);
    m_ignoredGeneration = m_generation;
    detach();
}

bool HwComposerScreenCapture::submit(HWComposerNativeWindowBuffer *buffer)
{
    ScopedCaptureContext scope(m_eglDisplay, m_encoderSurface, m_context);
    if (!scope.isCurrent()) {
        qWarning("screencap: cannot make the encoder Surface current (0x%x)", eglGetError());
        return false;
    }

    if (!render(buffer))
        return false;

    // The recorder's timestamps, including its end of recording, use
    // CLOCK_MONOTONIC as well.
    if (!m_eglPresentationTimeANDROID(m_eglDisplay, m_encoderSurface, monotonicNs())) {
        qWarning("screencap: eglPresentationTimeANDROID failed (0x%x)", eglGetError());
        return false;
    }

    const int64_t start = monotonicNs();
    if (!eglSwapBuffers(m_eglDisplay, m_encoderSurface)) {
        qWarning("screencap: eglSwapBuffers failed (0x%x)", eglGetError());
        return false;
    }
    const int64_t swapNs = monotonicNs() - start;
    if (swapNs > MaxSwapNs) {
        qWarning("screencap: the encoder blocked a swap for %" PRId64 " ms",
                 swapNs / 1000000);
        return false;
    }

    return true;
}

bool HwComposerScreenCapture::render(HWComposerNativeWindowBuffer *buffer)
{
    const GLuint texture = sourceTexture(buffer);
    if (!texture)
        return false;
    if (!m_program && !(m_program = createProgram()))
        return false;

    // EGL leaves the buffer undefined after a swap, so clear the bars around
    // the picture. A full clear also spares the GPU loading the old contents.
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    const int corners = m_fit.rotation == 90 ? 1 : m_fit.rotation == 270 ? 2 : 0;
    const QRect &vp = m_fit.viewport;
    // glViewport() counts y from the bottom.
    glViewport(vp.x(), m_height - vp.y() - vp.height(), vp.width(), vp.height());
    glUseProgram(m_program);
    glBindTexture(GL_TEXTURE_2D, texture);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, Positions);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, TexCoords[corners]);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        qWarning("screencap: drawing failed (0x%x)", error);
        return false;
    }
    return true;
}

GLuint HwComposerScreenCapture::sourceTexture(HWComposerNativeWindowBuffer *buffer)
{
    auto it = m_sources.find(buffer->handle);
    if (it != m_sources.end())
        return it->second.texture;

    static const EGLint imageAttribs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    const EGLImageKHR image = m_eglCreateImageKHR(m_eglDisplay, EGL_NO_CONTEXT,
        EGL_NATIVE_BUFFER_ANDROID,
        reinterpret_cast<EGLClientBuffer>(buffer->getNativeBuffer()), imageAttribs);
    if (image == EGL_NO_IMAGE_KHR) {
        qWarning("screencap: cannot import a display buffer (0x%x)", eglGetError());
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    m_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, reinterpret_cast<GLeglImageOES>(image));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        qWarning("screencap: cannot bind a display buffer to a texture (0x%x)", error);
        glDeleteTextures(1, &texture);
        m_eglDestroyImageKHR(m_eglDisplay, image);
        return 0;
    }

    Source source = { image, texture };
    m_sources[buffer->handle] = source;
    return texture;
}
