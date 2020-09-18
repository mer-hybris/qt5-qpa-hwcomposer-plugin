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

#ifndef EGLFSHWCINTEGRATION_H
#define EGLFSHWCINTEGRATION_H

#include <private/qeglfsdeviceintegration_p.h>

#include "hwcomposer_screeninfo.h"

class HwComposerBackend;

namespace HwcInterface {
    class Compositor;
}

class EglFsHwcIntegration : public QEglFSDeviceIntegration
{
public:
    EglFsHwcIntegration();
    ~EglFsHwcIntegration();

    // QEglFSDeviceIntegration
    void platformInit() override;
    void platformDestroy() override;
    EGLNativeDisplayType platformDisplay() const override;
    EGLDisplay createDisplay(EGLNativeDisplayType nativeDisplay) override;
    bool usesDefaultScreen() override;
    void screenInit() override;
    void screenDestroy() override;
    QSizeF physicalScreenSize() const override;
    QSize screenSize() const override;
    QDpi logicalDpi() const override;
    qreal pixelDensity() const override;
    Qt::ScreenOrientation nativeOrientation() const override;
    Qt::ScreenOrientation orientation() const override;
    int screenDepth() const override;
    QImage::Format screenFormat() const override;
    qreal refreshRate() const override;
    QSurfaceFormat surfaceFormatFor(const QSurfaceFormat &inputFormat) const override;
    EGLint surfaceType() const override;
    QEglFSWindow *createWindow(QWindow *window) const override;
    EGLNativeWindowType createNativeWindow(
            QPlatformWindow *platformWindow, const QSize &size, const QSurfaceFormat &format) override;
    EGLNativeWindowType createNativeOffscreenWindow(const QSurfaceFormat &format) override;
    void destroyNativeWindow(EGLNativeWindowType window) override;
    bool hasCapability(QPlatformIntegration::Capability cap) const override;
    QPlatformCursor *createCursor(QPlatformScreen *screen) const override;
    bool filterConfig(EGLDisplay display, EGLConfig config) const override;
    void waitForVSync(QPlatformSurface *surface) const override;
    void presentBuffer(QPlatformSurface *surface) override;
    QByteArray fbDeviceName() const override;
    int framebufferIndex() const override;
    bool supportsPBuffers() const override;
    bool supportsSurfacelessContexts() const override;

    void *wlDisplay() const override;

    bool swapBuffers(QPlatformSurface *surface) override;
    void *nativeResourceForIntegration(const QByteArray &resource) override;

private:
    QSocketNotifier m_exitNotifier;
    HwComposerScreenInfo info;
    HwComposerBackend *backend = nullptr;
    bool display_off = false;
    bool window_created = false;
    qreal fps = 0.0;
};

#endif

