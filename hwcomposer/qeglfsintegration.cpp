/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the plugins of the Qt Toolkit.
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

#include "qeglfsintegration.h"

#include "qeglfswindow.h"
#include "qeglfsbackingstore.h"

#include <QtGui/private/qguiapplication_p.h>

#include <QtPlatformSupport/private/qgenericunixfontdatabase_p.h>
#include <QtPlatformSupport/private/qgenericunixeventdispatcher_p.h>
#include <QtPlatformSupport/private/qgenericunixthemes_p.h>
#include <QtPlatformSupport/private/qeglconvenience_p.h>
#include <QtPlatformSupport/private/qeglplatformcontext_p.h>
#include <QtPlatformSupport/private/qeglpbuffer_p.h>

#include <qpa/qplatformwindow.h>
#include <qpa/qplatformservices.h>
#include <QtGui/QSurfaceFormat>
#include <QtGui/QOpenGLContext>
#include <QtGui/QScreen>
#include <QtGui/QOffscreenSurface>

#include <qpa/qplatforminputcontextfactory_p.h>

#include "qeglfscontext.h"

#include <EGL/egl.h>

QT_BEGIN_NAMESPACE

class GenericEglFSTheme: public QGenericUnixTheme
{
public:
    static QStringList themeNames()
    {
        return QStringList() << QLatin1String("generic_eglfs");
    }
};

QEglFSIntegration::QEglFSIntegration()
    : mHwc(NULL)
    , mEventDispatcher(createUnixEventDispatcher())
    , mFontDb(new QGenericUnixFontDatabase())
{
#if QT_VERSION < QT_VERSION_CHECK(5, 2, 0)
    QGuiApplicationPrivate::instance()->setEventDispatcher(mEventDispatcher);
#endif

    mHwc = new HwComposerContext();

    EGLint major, minor;

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        qWarning("Could not bind GL_ES API\n");
        qFatal("EGL error");
    }

    mDisplay = eglGetDisplay(mHwc->platformDisplay());
    if (mDisplay == EGL_NO_DISPLAY) {
        qWarning("Could not open egl display\n");
        qFatal("EGL error");
    }

    if (!eglInitialize(mDisplay, &major, &minor)) {
        qWarning("Could not initialize egl display\n");
        qFatal("EGL error");
    }

    mScreen = new QEglFSScreen(mHwc, mDisplay);
    screenAdded(mScreen);

    mInputContext = QPlatformInputContextFactory::create();
}

QEglFSIntegration::~QEglFSIntegration()
{
#if QT_VERSION >= 0x050500
    destroyScreen(mScreen);
#else
    delete mScreen;
#endif

    eglTerminate(mDisplay);
    delete mHwc;
}

bool QEglFSIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
        case ThreadedPixmaps:
        case OpenGL:
        case ThreadedOpenGL:
        case BufferQueueingOpenGL:
            return true;

        default:
            return QPlatformIntegration::hasCapability(cap);
    }
}

QPlatformWindow *QEglFSIntegration::createPlatformWindow(QWindow *window) const
{
    QEglFSWindow *w = new QEglFSWindow(mHwc, window);
    w->create();
    w->requestActivateWindow();
    return w;
}

QPlatformBackingStore *QEglFSIntegration::createPlatformBackingStore(QWindow *window) const
{
    return new QEglFSBackingStore(window);
}

QPlatformOpenGLContext *QEglFSIntegration::createPlatformOpenGLContext(QOpenGLContext *context) const
{
    return new QEglFSContext(mHwc, mHwc->surfaceFormatFor(context->format()), context->shareHandle(), mDisplay);
}

QPlatformOffscreenSurface *QEglFSIntegration::createPlatformOffscreenSurface(QOffscreenSurface *surface) const
{
    QEglFSScreen *screen = static_cast<QEglFSScreen *>(surface->screen()->handle());
    return new QEGLPbuffer(screen->display(), mHwc->surfaceFormatFor(surface->requestedFormat()), surface);
}

QPlatformFontDatabase *QEglFSIntegration::fontDatabase() const
{
    return mFontDb;
}

#if QT_VERSION < QT_VERSION_CHECK(5, 2, 0)
QAbstractEventDispatcher *QEglFSIntegration::guiThreadEventDispatcher() const
#else
QAbstractEventDispatcher *QEglFSIntegration::createEventDispatcher() const
#endif
{
    return mEventDispatcher;
}

QVariant QEglFSIntegration::styleHint(QPlatformIntegration::StyleHint hint) const
{
    if (hint == QPlatformIntegration::ShowIsFullScreen)
        return true;

    return QPlatformIntegration::styleHint(hint);
}

QPlatformNativeInterface *QEglFSIntegration::nativeInterface() const
{
    return const_cast<QEglFSIntegration *>(this);
}

void *QEglFSIntegration::nativeResourceForIntegration(const QByteArray &resource)
{
    QByteArray lowerCaseResource = resource.toLower();

    if (lowerCaseResource == "egldisplay") {
        return static_cast<QEglFSScreen *>(mScreen)->display();
    } else if (lowerCaseResource == "displayoff") {
        // Called from lipstick to turn off the display (src/homeapplication.cpp)
        mHwc->sleepDisplay(true);
    } else if (lowerCaseResource == "displayon") {
        // Called from lipstick to turn on the display (src/homeapplication.cpp)
        mHwc->sleepDisplay(false);
    }

    return NULL;
}

void *QEglFSIntegration::nativeResourceForWindow(const QByteArray &resource, QWindow *window)
{
    QByteArray lowerCaseResource = resource.toLower();

    if (lowerCaseResource == "egldisplay") {
        if (window && window->handle())
            return static_cast<QEglFSScreen *>(window->handle()->screen())->display();
        else
            return static_cast<QEglFSScreen *>(mScreen)->display();
    }

    return 0;
}

void *QEglFSIntegration::nativeResourceForContext(const QByteArray &resource, QOpenGLContext *context)
{
    QByteArray lowerCaseResource = resource.toLower();

    QEGLPlatformContext *handle = static_cast<QEGLPlatformContext *>(context->handle());

    if (!handle)
        return 0;

    if (lowerCaseResource == "eglcontext")
        return handle->eglContext();

    return 0;
}

EGLConfig QEglFSIntegration::chooseConfig(EGLDisplay display, const QSurfaceFormat &format)
{
    QEglConfigChooser chooser(display);
    chooser.setSurfaceFormat(format);
    return chooser.chooseConfig();
}

QStringList QEglFSIntegration::themeNames() const
{
    return GenericEglFSTheme::themeNames();
}

QPlatformTheme *QEglFSIntegration::createPlatformTheme(const QString &name) const
{
    if (name == QLatin1String("generic_eglfs"))
        return new GenericEglFSTheme;

    return GenericEglFSTheme::createUnixTheme(name);
}

QT_END_NAMESPACE
