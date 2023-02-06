/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** Based on qeglfshooks_stub.cpp, part of the plugins of the Qt Toolkit.
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

#include "hwcomposer_context.h"

#include "qeglfscontext.h"
#include "hwcomposer_screeninfo.h"
#include "hwcomposer_backend.h"

#include <qcoreapplication.h>

QT_BEGIN_NAMESPACE

HwComposerContext::HwComposerContext()
    : info(NULL)
    , backend(NULL)
    , display_off(false)
    , window_created(false)
    , fps(0)
{
    // This actually opens the hwcomposer device
    backend = HwComposerBackend::create();
    HWC_PLUGIN_ASSERT_NOT_NULL(backend);

    fps = backend->refreshRate();

    info = new HwComposerScreenInfo(backend);
}

HwComposerContext::~HwComposerContext()
{
    // Properly clean up hwcomposer backend
    HwComposerBackend::destroy(backend);

    // Free framebuffer device parameters info
    delete info;
}

EGLNativeDisplayType HwComposerContext::platformDisplay() const
{
    return backend->display();
}

QSizeF HwComposerContext::physicalScreenSize() const
{
    return info->physicalScreenSize();
}

int HwComposerContext::screenDepth() const
{
    return info->screenDepth();
}

QSize HwComposerContext::screenSize() const
{
    return info->screenSize();
}

QSurfaceFormat HwComposerContext::surfaceFormatFor(const QSurfaceFormat &inputFormat) const
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

EGLNativeWindowType HwComposerContext::createNativeWindow(const QSurfaceFormat &format)
{
    Q_UNUSED(format);

    if (window_created) {
        HWC_PLUGIN_FATAL("There can only be one window, someone tried to create more.");
    }

    window_created = true;
    QSize size = screenSize();
    return backend->createWindow(size.width(), size.height());
}

void HwComposerContext::destroyNativeWindow(EGLNativeWindowType window)
{
    return backend->destroyWindow(window);
}

void HwComposerContext::swapToWindow(QEglFSContext *context, QPlatformSurface *surface)
{
    if (display_off) {
        qWarning("Swap requested while display is off");
        return;
    }

    EGLDisplay egl_display = context->eglDisplay();
    EGLSurface egl_surface = context->eglSurfaceForPlatformSurface(surface);
    return backend->swap(egl_display, egl_surface);
}

void HwComposerContext::sleepDisplay(bool sleep)
{
    if (sleep) {
        qDebug("sleepDisplay");
        display_off = true;
    } else {
        qDebug("unsleepDisplay");
        display_off = false;
    }

    backend->sleepDisplay(sleep);
}

qreal HwComposerContext::refreshRate() const
{
    return fps;
}

bool HwComposerContext::requestUpdate(QEglFSWindow *window)
{
    if (backend)
        return backend->requestUpdate(window);
    return false;
}

QT_END_NAMESPACE
