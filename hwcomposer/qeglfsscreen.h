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

#ifndef QEGLFSSCREEN_H
#define QEGLFSSCREEN_H

#include <qpa/qplatformscreen.h>
#include <QtCore/QTextStream>

#include "hwcomposer_context.h"

#include <EGL/egl.h>

#ifdef WITH_SENSORS
class QOrientationSensor;
#endif

QT_BEGIN_NAMESPACE

class QEglFSPageFlipper;
class QPlatformOpenGLContext;

#ifdef WITH_SENSORS
class QEglFSScreen : public QObject, public QPlatformScreen //huh: FullScreenScreen ;) just to follow namespace
{
    Q_OBJECT
#else
class QEglFSScreen : public QPlatformScreen //huh: FullScreenScreen ;) just to follow namespace
{
#endif
public:
    QEglFSScreen(HwComposerContext *hwc, EGLDisplay display);
    ~QEglFSScreen();

    QRect geometry() const;
    int depth() const;
    QImage::Format format() const;

    QSizeF physicalSize() const;
    QDpi logicalDpi() const;

    EGLDisplay display() const { return m_dpy; }

    qreal refreshRate() const;

#ifdef WITH_SENSORS
    Qt::ScreenOrientation orientation() const;
#endif

    QPlatformScreen::PowerState powerState() const override;
    void setPowerState(QPlatformScreen::PowerState state) override;

#if 0
    QPlatformScreenPageFlipper *pageFlipper() const;
#endif

private:
    HwComposerContext *m_hwc;
    QEglFSPageFlipper *m_pageFlipper;
    EGLDisplay m_dpy;
    PowerState m_powerState;
#ifdef WITH_SENSORS
    Qt::ScreenOrientation m_screenOrientation;
    QOrientationSensor *m_orientationSensor;

private Q_SLOTS:
    void orientationReadingChanged();
    void onStarted();
#endif
};

QT_END_NAMESPACE
#endif // QEGLFSSCREEN_H
