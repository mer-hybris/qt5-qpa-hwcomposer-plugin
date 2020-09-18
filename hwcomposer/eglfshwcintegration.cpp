/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** Copyright (C) 2013 Jolla Ltd.
** Contact: Thomas Perl <thomas.perl@jolla.com>
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

#include "eglfshwcintegration.h"
#include "eglfshwcwindow.h"
#include "hwcomposer_backend.h"
#include "hwcinterface.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>

#include <inttypes.h>
#include <unistd.h>
#include <signal.h>

static int exitSignalFd = -1;

static void exit_qt_gracefully(int sig)
{
    int64_t eventData = sig;
    const ssize_t size = ::write(exitSignalFd, &eventData, sizeof(eventData));
    Q_ASSERT(size == sizeof(eventData));
}

EglFsHwcIntegration::EglFsHwcIntegration()
    : m_exitNotifier(::eventfd(0, EFD_NONBLOCK), QSocketNotifier::Read)
{
    exitSignalFd = m_exitNotifier.socket();

    // We need to catch the SIGTERM and SIGINT signals, so that we can do a
    // proper shutdown of Qt and the plugin, and avoid crashes, hangs and
    // reboots in cases where we don't properly close the hwcomposer.
    struct sigaction new_action;
    new_action.sa_handler = exit_qt_gracefully;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGTERM, &new_action, NULL);
    sigaction(SIGINT, &new_action, NULL);

    QObject::connect(&m_exitNotifier, &QSocketNotifier::activated, []() {
        int64_t eventData = 0;
        const ssize_t size = ::read(exitSignalFd, &eventData, sizeof(eventData));
        Q_ASSERT(size == sizeof(eventData));

        QCoreApplication::exit(0);
    });

}

EglFsHwcIntegration::~EglFsHwcIntegration()
{
    HwComposerBackend::destroy(backend);

    if (exitSignalFd >= 0) {
        ::close(exitSignalFd);
        exitSignalFd = 0;
    }
}

void EglFsHwcIntegration::platformInit()
{
    // This actually opens the hwcomposer device
    backend = HwComposerBackend::create();
    HWC_PLUGIN_ASSERT_NOT_NULL(backend);

    fps = backend->refreshRate();
}

void EglFsHwcIntegration::platformDestroy()
{
    HwComposerBackend::destroy(backend);
    backend = nullptr;
}

EGLNativeDisplayType EglFsHwcIntegration::platformDisplay() const
{
    return backend->display();
}

EGLDisplay EglFsHwcIntegration::createDisplay(EGLNativeDisplayType nativeDisplay)
{
    return QEglFSDeviceIntegration::createDisplay(nativeDisplay);
}

bool EglFsHwcIntegration::usesDefaultScreen()
{
    return QEglFSDeviceIntegration::usesDefaultScreen();
}

void EglFsHwcIntegration::screenInit()
{
    QEglFSDeviceIntegration::screenInit();
}

void EglFsHwcIntegration::screenDestroy()
{
    QEglFSDeviceIntegration::screenDestroy();
}

QSizeF EglFsHwcIntegration::physicalScreenSize() const
{
    return info.physicalScreenSize();
}

QSize EglFsHwcIntegration::screenSize() const
{
    return info.screenSize();
}

QDpi EglFsHwcIntegration::logicalDpi() const
{
    return QEglFSDeviceIntegration::logicalDpi();
}

qreal EglFsHwcIntegration::pixelDensity() const
{
    return QEglFSDeviceIntegration::pixelDensity();
}

Qt::ScreenOrientation EglFsHwcIntegration::nativeOrientation() const
{
    return QEglFSDeviceIntegration::nativeOrientation();
}

Qt::ScreenOrientation EglFsHwcIntegration::orientation() const
{
    return QEglFSDeviceIntegration::orientation();
}

int EglFsHwcIntegration::screenDepth() const
{
    return info.screenDepth();
}

QImage::Format EglFsHwcIntegration::screenFormat() const
{
    switch (info.screenDepth()) {
        case 16:
            return QImage::Format_RGB16;
        default:
            return QImage::Format_RGB32;
    }
}

qreal EglFsHwcIntegration::refreshRate() const
{
    return fps;
}

QSurfaceFormat EglFsHwcIntegration::surfaceFormatFor(const QSurfaceFormat &inputFormat) const
{
    QSurfaceFormat newFormat = inputFormat;
    if (screenDepth() == 16) {
        newFormat.setRedBufferSize(5);
        newFormat.setGreenBufferSize(6);
        newFormat.setBlueBufferSize(5);
    } else {
        newFormat.setStencilBufferSize(8);
        newFormat.setAlphaBufferSize(8);
        newFormat.setRedBufferSize(8);
        newFormat.setGreenBufferSize(8);
        newFormat.setBlueBufferSize(8);
    }

    return newFormat;
}

EGLint EglFsHwcIntegration::surfaceType() const
{
    return QEglFSDeviceIntegration::surfaceType();
}

QEglFSWindow *EglFsHwcIntegration::createWindow(QWindow *window) const
{
    return new EglFsHwcWindow(backend, window);
}

EGLNativeWindowType EglFsHwcIntegration::createNativeWindow(
        QPlatformWindow *platformWindow, const QSize &size, const QSurfaceFormat &format)
{
    Q_UNUSED(platformWindow);
    Q_UNUSED(format);

    if (window_created) {
        HWC_PLUGIN_FATAL("There can only be one window, someone tried to create more.");
    }

    window_created = true;
    return backend->createWindow(size.width(), size.height());
}

EGLNativeWindowType EglFsHwcIntegration::createNativeOffscreenWindow(
        const QSurfaceFormat &format)
{
    return QEglFSDeviceIntegration::createNativeOffscreenWindow(format);
}

void EglFsHwcIntegration::destroyNativeWindow(EGLNativeWindowType window)
{
    window_created = false;
    return backend->destroyWindow(window);
}

bool EglFsHwcIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
        case QPlatformIntegration::ThreadedPixmaps:
        case QPlatformIntegration::OpenGL:
        case QPlatformIntegration::ThreadedOpenGL:
        case QPlatformIntegration::BufferQueueingOpenGL:
            return true;
        default:
            return QEglFSDeviceIntegration::hasCapability(cap);
    }
}

QPlatformCursor *EglFsHwcIntegration::createCursor(QPlatformScreen *screen) const
{
    return QEglFSDeviceIntegration::createCursor(screen);
}

bool EglFsHwcIntegration::filterConfig(EGLDisplay display, EGLConfig config) const
{
    return QEglFSDeviceIntegration::filterConfig(display, config);
}

void EglFsHwcIntegration::waitForVSync(QPlatformSurface *surface) const
{
    QEglFSDeviceIntegration::waitForVSync(surface);
}

bool EglFsHwcIntegration::swapBuffers(QPlatformSurface *surface)
{
    if (surface->surface()->surfaceClass() == QSurface::Window) {
        if (display_off) {
            qCWarning(QPA_LOG_HWC, "Swap requested while display is off");
            return true;
        }

//        EGLDisplay egl_display = context->eglDisplay();

//        EGLSurface egl_surface = context->eglSurfaceForPlatformSurface(surface);

        backend->swap(eglGetDisplay(backend->display()), static_cast<QEglFSWindow *>(surface)->surface());

        return true;
//        return false;
    } else {
        return false;
    }
}

void EglFsHwcIntegration::presentBuffer(QPlatformSurface *surface)
{
    QEglFSDeviceIntegration::presentBuffer(surface);
}

QByteArray EglFsHwcIntegration::fbDeviceName() const
{
    return QEglFSDeviceIntegration::fbDeviceName();
}

int EglFsHwcIntegration::framebufferIndex() const
{
    return QEglFSDeviceIntegration::framebufferIndex();
}

bool EglFsHwcIntegration::supportsPBuffers() const
{
    return QEglFSDeviceIntegration::supportsPBuffers();
}

bool EglFsHwcIntegration::supportsSurfacelessContexts() const
{
    return QEglFSDeviceIntegration::supportsSurfacelessContexts();
}

void *EglFsHwcIntegration::wlDisplay() const
{
    return QEglFSDeviceIntegration::wlDisplay();
}

void *EglFsHwcIntegration::nativeResourceForIntegration(const QByteArray &resource)
{
    const QByteArray lowerCaseResource = resource.toLower();

    if (lowerCaseResource == "displayoff") {
        // Called from lipstick to turn off the display (src/homeapplication.cpp)
        qCDebug(QPA_LOG_HWC, "sleepDisplay");
        display_off = true;

        backend->sleepDisplay(true);
    } else if (lowerCaseResource == "displayon") {
        // Called from lipstick to turn on the display (src/homeapplication.cpp)
        qCDebug(QPA_LOG_HWC, "unsleepDisplay");
        display_off = false;

        backend->sleepDisplay(false);
    } else if (lowerCaseResource == HWC_INTERFACE_STRING) {
        return backend->hwcInterface();
    }
    return nullptr;
}
