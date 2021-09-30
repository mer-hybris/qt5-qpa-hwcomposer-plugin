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

#ifndef HWCOMPOSER_BACKEND_H
#define HWCOMPOSER_BACKEND_H

#include <inttypes.h>
#include <sys/types.h>
#include <sync/sync.h>
#include <stdint.h>

#include <android-config.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <qdebug.h>

class QEglFSWindow;

// Evaluate "x", if it doesn't return zero, print a warning
#define HWC_PLUGIN_EXPECT_ZERO(x) \
    { int res; if ((res = (x)) != 0) \
        qWarning("QPA-HWC: %s in %s returned %i", (#x), __func__, res); }

// Evaluate "x", if it isn't NULL, print a warning
#define HWC_PLUGIN_EXPECT_NULL(x) \
    { void *res; if ((res = (x)) != NULL) \
        qWarning("QPA-HWC: %s in %s returned %" PRIxPTR, (#x), __func__, (intptr_t)res); }

// Evaluate "x", if it is NULL, exit with a fatal error
#define HWC_PLUGIN_FATAL(x) \
    qFatal("QPA-HWC: %s in %s", x, __func__)

// Evaluate "x", if it is NULL, exit with a fatal error
#define HWC_PLUGIN_ASSERT_NOT_NULL(x) \
    { void *res; if ((res = (x)) == NULL) \
        qFatal("QPA-HWC: %s in %s returned %" PRIxPTR, (#x), __func__, (intptr_t)res); }

// Evaluate "x", if it doesn't return zero, exit with a fatal error
#define HWC_PLUGIN_ASSERT_ZERO(x) \
    { int res; if ((res = (x)) != 0) \
        qFatal("QPA-HWC: %s in %s returned %i", (#x), __func__, res); }

inline static uint32_t interpreted_version(hw_device_t *hwc_device)
{
    uint32_t version = hwc_device->version;

    if ((version & 0xffff0000) == 0) {
        // Assume header version is always 1
        uint32_t header_version = 1;

        // Legacy version encoding
        version = (version << 16) | header_version;
    }
    return version;
}


class HwComposerBackend {
public:
    // Factory method to get the right hwcomposer backend version
    static HwComposerBackend *create();
    static void destroy(HwComposerBackend *backend);

    // Public API that needs to be implemented by a versioned backend
    virtual EGLNativeDisplayType display() = 0;
    virtual EGLNativeWindowType createWindow(int width, int height) = 0;
    virtual void destroyWindow(EGLNativeWindowType window) = 0;
    virtual void swap(EGLNativeDisplayType display, EGLSurface surface) = 0;
    virtual void sleepDisplay(bool sleep) = 0;
    virtual float refreshRate() = 0;

    virtual bool getScreenSizes(int *width, int *height, float *physical_width, float *physical_height) = 0;

    virtual bool requestUpdate(QEglFSWindow *) { return false; }

protected:
    HwComposerBackend(hw_module_t *hwc_module, void *libmsf);
    virtual ~HwComposerBackend();

    hw_module_t *hwc_module;
    void *libminisf;
};

#endif /* HWCOMPOSER_BACKEND_H */
