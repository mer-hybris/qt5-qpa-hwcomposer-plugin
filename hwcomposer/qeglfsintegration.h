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

#ifndef QEGLFSINTEGRATION_H
#define QEGLFSINTEGRATION_H

#include "qeglfsscreen.h"

#include "hwcomposer_context.h"

#include <qpa/qplatformintegration.h>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformscreen.h>

QT_BEGIN_NAMESPACE

class QEglFSIntegration : public QPlatformIntegration, public QPlatformNativeInterface
{
public:
    QEglFSIntegration();
    ~QEglFSIntegration();

    bool hasCapability(QPlatformIntegration::Capability cap) const;

    QPlatformWindow *createPlatformWindow(QWindow *window) const;
    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const;
    QPlatformOpenGLContext *createPlatformOpenGLContext(QOpenGLContext *context) const;
    QPlatformOffscreenSurface *createPlatformOffscreenSurface(QOffscreenSurface *surface) const;
    QPlatformNativeInterface *nativeInterface() const;

    QPlatformFontDatabase *fontDatabase() const;

#if QT_VERSION < QT_VERSION_CHECK(5, 2, 0)
    QAbstractEventDispatcher *guiThreadEventDispatcher() const;
#else
    QAbstractEventDispatcher *createEventDispatcher() const;
#endif

    QVariant styleHint(QPlatformIntegration::StyleHint hint) const;

    // QPlatformNativeInterface
    void *nativeResourceForIntegration(const QByteArray &resource);
    void *nativeResourceForWindow(const QByteArray &resource, QWindow *window) Q_DECL_OVERRIDE;
    void *nativeResourceForContext(const QByteArray &resource, QOpenGLContext *context);

    QPlatformScreen *screen() const { return mScreen; }
    static EGLConfig* chooseConfig(EGLDisplay display, const QSurfaceFormat &format);

    EGLDisplay display() const { return mDisplay; }

    QPlatformInputContext *inputContext() const { return mInputContext; }

    QStringList themeNames() const;

    QPlatformTheme *createPlatformTheme(const QString &name) const;

private:
    HwComposerContext *mHwc;
    EGLDisplay mDisplay;
    QAbstractEventDispatcher *mEventDispatcher;
    QPlatformFontDatabase *mFontDb;
    QPlatformScreen *mScreen;
    QPlatformInputContext *mInputContext;
};

QT_END_NAMESPACE

#endif // QEGLFSINTEGRATION_H
