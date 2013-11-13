/****************************************************************************
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

#include "hwcomposer_screeninfo.h"

#include <private/qmath_p.h>
#include <private/qcore_unix_p.h>

#include <fcntl.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

/* Empty namespace */
namespace {

const char *ENV_VAR_EGLFS_PHYSICAL_WIDTH = "QT_QPA_EGLFS_PHYSICAL_WIDTH";
const char *ENV_VAR_EGLFS_PHYSICAL_HEIGHT = "QT_QPA_EGLFS_PHYSICAL_HEIGHT";
const char *ENV_VAR_EGLFS_WIDTH = "QT_QPA_EGLFS_WIDTH";
const char *ENV_VAR_EGLFS_HEIGHT = "QT_QPA_EGLFS_HEIGHT";
const char *ENV_VAR_EGLFS_DEPTH = "QT_QPA_EGLFS_DEPTH";

// Fallback source: Use default values and print a warning

class HwComposerScreenInfoFallbackSource {
public:
    QSizeF physicalScreenSize(QSize screenSize)
    {
        const int DEFAULT_PHYSICAL_DPI = 100;

        qWarning("EGLFS: Cannot determine physical screen size, assuming %d DPI",
                DEFAULT_PHYSICAL_DPI);
        qWarning("EGLFS: To override, set %s and %s (in mm)",
                ENV_VAR_EGLFS_PHYSICAL_WIDTH, ENV_VAR_EGLFS_PHYSICAL_HEIGHT);

        return QSizeF(screenSize.width() * Q_MM_PER_INCH / DEFAULT_PHYSICAL_DPI,
                      screenSize.height() * Q_MM_PER_INCH / DEFAULT_PHYSICAL_DPI);
    }

    QSize screenSize()
    {
        const int DEFAULT_WIDTH = 800;
        const int DEFAULT_HEIGHT = 600;

        qWarning("EGLFS: Cannot determine screen size, falling back to %dx%d",
                DEFAULT_WIDTH, DEFAULT_HEIGHT);
        qWarning("EGLFS: To override, set %s and %s (in pixels)",
                ENV_VAR_EGLFS_WIDTH, ENV_VAR_EGLFS_HEIGHT);

        return QSize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
    }

    int screenDepth()
    {
        const int DEFAULT_DEPTH = 32;

        qWarning("EGLFS: Cannot determine screen depth, falling back to %d",
                DEFAULT_DEPTH);
        qWarning("EGLFS: To override, set %s",
                ENV_VAR_EGLFS_DEPTH);

        return DEFAULT_DEPTH;
    }
};



// Framebuffer device source: Try to read values from fbdev

class HwComposerScreenInfoFbDevSource {
public:
    HwComposerScreenInfoFbDevSource()
        : m_valid(false)
    {
        const char *FRAMEBUFFER_DEVICE_NAME = "/dev/fb0";

        // Query variable screen information from framebuffer
        int framebuffer = qt_safe_open(FRAMEBUFFER_DEVICE_NAME, O_RDONLY);
        if (framebuffer != -1) {
            if (ioctl(framebuffer, FBIOGET_VSCREENINFO, &m_vinfo) != -1) {
                m_valid = true;
            } else {
                qWarning("EGLFS: Could not query variable screen info from %s",
                        FRAMEBUFFER_DEVICE_NAME);
            }
            close(framebuffer);
        } else {
            qWarning("EGLFS: Failed to open %s", FRAMEBUFFER_DEVICE_NAME);
        }
    }

    bool isValid()
    {
        return m_valid;
    }

    QSizeF physicalScreenSize()
    {
        return QSizeF(m_vinfo.width, m_vinfo.height);
    }

    QSize screenSize()
    {
        return QSize(m_vinfo.xres, m_vinfo.yres);
    }

    int screenDepth()
    {
        return m_vinfo.bits_per_pixel;
    }

private:
    struct fb_var_screeninfo m_vinfo;
    bool m_valid;
};



// Environment variable source: Use override values from env vars

class HwComposerScreenInfoEnvironmentSource {
public:
    HwComposerScreenInfoEnvironmentSource()
        : m_physicalWidth(qgetenv(ENV_VAR_EGLFS_PHYSICAL_WIDTH).toInt())
        , m_physicalHeight(qgetenv(ENV_VAR_EGLFS_PHYSICAL_HEIGHT).toInt())
        , m_width(qgetenv(ENV_VAR_EGLFS_WIDTH).toInt())
        , m_height(qgetenv(ENV_VAR_EGLFS_HEIGHT).toInt())
        , m_depth(qgetenv(ENV_VAR_EGLFS_DEPTH).toInt())
    {
    }

    bool hasPhysicalScreenSize()
    {
        return ((m_physicalWidth != 0) && (m_physicalHeight != 0));
    }

    QSizeF physicalScreenSize()
    {
        return QSizeF(m_physicalWidth, m_physicalHeight);
    }

    bool hasScreenSize()
    {
        return ((m_width != 0 && m_height != 0));
    }

    QSize screenSize()
    {
        return QSize(m_width, m_height);
    }

    bool hasScreenDepth()
    {
        return (m_depth != 0);
    }

    int screenDepth()
    {
        return m_depth;
    }

private:
    int m_physicalWidth;
    int m_physicalHeight;
    int m_width;
    int m_height;
    int m_depth;
};


} /* empty namespace */


QT_BEGIN_NAMESPACE

HwComposerScreenInfo::HwComposerScreenInfo()
{
    /**
     * Look up the values in the following order of preference:
     *
     *  1. Environment variables can override everything
     *  2. fbdev via FBIOGET_VSCREENINFO is preferred otherwise
     *  3. Fallback values (with warnings) if 1. and 2. fail
     **/
    HwComposerScreenInfoEnvironmentSource envSource;
    HwComposerScreenInfoFbDevSource fbdevSource;
    HwComposerScreenInfoFallbackSource fallbackSource;

    if (envSource.hasScreenSize()) {
        m_screenSize = envSource.screenSize();
    } else if (fbdevSource.isValid()) {
        m_screenSize = fbdevSource.screenSize();
    } else {
        m_screenSize = fallbackSource.screenSize();
    }

    if (envSource.hasPhysicalScreenSize()) {
        m_physicalScreenSize = envSource.physicalScreenSize();
    } else if (fbdevSource.isValid()) {
        m_physicalScreenSize = fbdevSource.physicalScreenSize();
    } else {
        m_physicalScreenSize = fallbackSource.physicalScreenSize(m_screenSize);
    }

    if (envSource.hasScreenDepth()) {
        m_screenDepth = envSource.screenDepth();
    } else if (fbdevSource.isValid()) {
        m_screenDepth = fbdevSource.screenDepth();
    } else {
        m_screenDepth = fallbackSource.screenDepth();
    }

    qDebug() << "EGLFS: Screen Info";
    qDebug() << " - Physical size:" << m_physicalScreenSize;
    qDebug() << " - Screen size:" << m_screenSize;
    qDebug() << " - Screen depth:" << m_screenDepth;
}

QT_END_NAMESPACE
