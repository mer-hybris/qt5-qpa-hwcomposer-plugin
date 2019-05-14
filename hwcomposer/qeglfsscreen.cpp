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

#include "qeglfsscreen.h"
#include "qeglfswindow.h"

#include <private/qmath_p.h>

#ifdef WITH_SENSORS
#include <QtSensors/QSensorManager>
#include <QtSensors/QOrientationSensor>
#include <QtSensors/QOrientationFilter>
#include <QtSensors/QOrientationReading>
#include <qpa/qwindowsysteminterface.h>

#include <QTimer>
#endif

QT_BEGIN_NAMESPACE

QEglFSScreen::QEglFSScreen(HwComposerContext *hwc, EGLDisplay dpy)
    : m_hwc(hwc)
    , m_dpy(dpy)
#ifdef WITH_SENSORS
    , m_screenOrientation(Qt::PrimaryOrientation)
    , m_orientationSensor(new QOrientationSensor(this))
#endif
{
#ifdef QEGL_EXTRA_DEBUG
    qWarning("QEglScreen %p\n", this);
#endif
}

QEglFSScreen::~QEglFSScreen()
{
#ifdef WITH_SENSORS
    if (m_orientationSensor) {
        m_orientationSensor->stop();
        delete m_orientationSensor; m_orientationSensor = NULL;
    }
#endif
}

#ifdef WITH_SENSORS
void QEglFSScreen::onStarted()
{
    if (!m_orientationSensor->isActive()) {
        m_orientationSensor->start();
    }
}
#endif

QRect QEglFSScreen::geometry() const
{
    return QRect(QPoint(0, 0), m_hwc->screenSize());
}

int QEglFSScreen::depth() const
{
    return m_hwc->screenDepth();
}

QImage::Format QEglFSScreen::format() const
{
    switch (m_hwc->screenDepth()) {
        case 16:
            return QImage::Format_RGB16;
        default:
            return QImage::Format_RGB32;
    }
}

QSizeF QEglFSScreen::physicalSize() const
{
    return m_hwc->physicalScreenSize();
}

QDpi QEglFSScreen::logicalDpi() const
{
    QSizeF ps = m_hwc->physicalScreenSize();
    QSize s = m_hwc->screenSize();

    return QDpi(Q_MM_PER_INCH * s.width() / ps.width(),
                Q_MM_PER_INCH * s.height() / ps.height());
}


qreal QEglFSScreen::refreshRate() const
{
    return m_hwc->refreshRate();
}

#ifdef WITH_SENSORS
void QEglFSScreen::orientationReadingChanged()
{
    QSize screenSize = m_hwc->screenSize();
    Qt::ScreenOrientation screenPrimaryOrientation = Qt::PortraitOrientation;
    if (screenSize.width() > screenSize.height()) {
        screenPrimaryOrientation = Qt::LandscapeOrientation;
    }

    QOrientationReading *orientationReading = m_orientationSensor->reading();
    QOrientationReading::Orientation currentOrientation = orientationReading->orientation();

    switch (currentOrientation) {
    case QOrientationReading::TopUp:   /* 0 */
        m_screenOrientation = screenPrimaryOrientation;
        break;
    case QOrientationReading::LeftUp:  /* 90 clockwise */
        m_screenOrientation = screenPrimaryOrientation == Qt::PortraitOrientation ? 
            Qt::InvertedLandscapeOrientation : Qt::PortraitOrientation;
        break;
    case QOrientationReading::TopDown: /* 180 */
        m_screenOrientation = screenPrimaryOrientation == Qt::PortraitOrientation ? 
            Qt::InvertedPortraitOrientation : Qt::InvertedLandscapeOrientation;
        break;
    case QOrientationReading::RightUp: /* 270 clockwise */
        m_screenOrientation = screenPrimaryOrientation == Qt::PortraitOrientation ? 
            Qt::LandscapeOrientation : Qt::InvertedPortraitOrientation;
        break;
    }

    QWindowSystemInterface::handleScreenOrientationChange(QPlatformScreen::screen(), m_screenOrientation);
}

Qt::ScreenOrientation QEglFSScreen::orientation() const
{
    return m_screenOrientation;
}
#endif

QT_END_NAMESPACE
