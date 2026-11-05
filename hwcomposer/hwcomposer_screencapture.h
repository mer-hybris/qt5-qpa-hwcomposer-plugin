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

#ifndef HWCOMPOSER_SCREENCAPTURE_H
#define HWCOMPOSER_SCREENCAPTURE_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cutils/native_handle.h>
#include <stdint.h>

#include <QtCore/QObject>
#include <QtCore/QRect>
#include <QtCore/QSize>

#include <map>

class HWComposerNativeWindowBuffer;
struct MinisfScreenCaptureSessionInfo;

// Where HwComposerScreenCapture draws the display in a session's frame.
struct CaptureFit {
    int rotation;    // 0, 90 or 270
    QRect viewport;  // top-left origin
};

// Turns the display by landscapeRotation if its orientation differs from the content rect's
// (an empty one is the whole frame), then fits it in, centred, sizes rounded down to even.
inline CaptureFit captureFit(const QSize &display, const QSize &session,
                             const QRect &content, int landscapeRotation)
{
    const QRect rect = content.isEmpty() ? QRect(QPoint(), session) : content;
    const bool rotate = (rect.width() > rect.height() && display.width() < display.height()) ||
                        (rect.width() < rect.height() && display.width() > display.height());
    const QSize source = rotate ? display.transposed() : display;

    QSize fitted = source.scaled(rect.size(), Qt::KeepAspectRatio);
    fitted.rwidth() &= ~1;
    fitted.rheight() &= ~1;

    return {
        rotate ? landscapeRotation : 0,
        QRect(rect.x() + (rect.width() - fitted.width()) / 2,
              rect.y() + (rect.height() - fitted.height()) / 2,
              fitted.width(), fitted.height())
    };
}

/*
 * Renders the display into the encoder Surface of a screen recorder (droidmedia's
 * droid_media_screen_capture_*), following its sailfish.screencap session through libminisf.
 * After every primary swap, the buffer just presented is drawn as one quad fitted by
 * captureFit(), with no CPU copy; every rendered frame is sent. Only DISPLAY sessions are
 * rendered. Session changes and frame requests repaint the exposed windows (event()).
 */
class HwComposerScreenCapture : public QObject
{
public:
    // Returns nullptr unless QPA_HWC_SCREENCAP is 1 or true. On the GUI thread.
    static HwComposerScreenCapture *create(int displayWidth, int displayHeight);
    ~HwComposerScreenCapture();

    // From present(), inside the primary eglSwapBuffers().
    void bufferPresented(HWComposerNativeWindowBuffer *buffer, int acquireFenceFd);
    // After the primary eglSwapBuffers() has returned.
    void captureFrame();

    bool event(QEvent *e) override;

private:
    HwComposerScreenCapture(int displayWidth, int displayHeight, int landscapeRotation);

    bool resolve(void *libminisf);
    static void sessionCallback(void *user, int event);
    void checkSession();
    void attach(const MinisfScreenCaptureSessionInfo &info, const CaptureFit &fit);
    void detach();
    void fail();
    bool submit(HWComposerNativeWindowBuffer *buffer);
    bool render(HWComposerNativeWindowBuffer *buffer);
    GLuint sourceTexture(HWComposerNativeWindowBuffer *buffer);

    // libminisf
    int (*m_sessionQuery)(MinisfScreenCaptureSessionInfo *info) = nullptr;
    void *(*m_targetAcquireGeneration)(uint64_t generation) = nullptr;
    void *(*m_targetNativeWindow)(void *target) = nullptr;
    void (*m_targetRelease)(void *target) = nullptr;
    void (*m_setCallback)(void (*callback)(void *user, int event), void *user) = nullptr;

    // EGL and GL extensions
    PFNEGLCREATEIMAGEKHRPROC m_eglCreateImageKHR = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC m_eglDestroyImageKHR = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC m_glEGLImageTargetTexture2DOES = nullptr;
    PFNEGLPRESENTATIONTIMEANDROIDPROC m_eglPresentationTimeANDROID = nullptr;

    const int m_displayWidth;
    const int m_displayHeight;
    const int m_landscapeRotation;

    // Last buffer handed to the display, and a copy of its acquire fence.
    HWComposerNativeWindowBuffer *m_buffer = nullptr;
    int m_fenceFd = -1;

    // The session this object gave up on, or does not render.
    uint64_t m_ignoredGeneration = 0;

    // The session this object is attached to.
    void *m_target = nullptr;
    uint64_t m_generation = 0;
    int m_width = 0;
    int m_height = 0;
    CaptureFit m_fit = {};

    EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_encoderSurface = EGL_NO_SURFACE;
    GLuint m_program = 0;

    struct Source {
        EGLImageKHR image;
        GLuint texture;
    };
    std::map<buffer_handle_t, Source> m_sources;
};

#endif /* HWCOMPOSER_SCREENCAPTURE_H */
