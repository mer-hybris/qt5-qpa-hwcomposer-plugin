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
#include <android-version.h>
#if ANDROID_VERSION_MAJOR <= 4
#include <hardware/hwcomposer_defs.h>
#ifdef HWC_DEVICE_API_VERSION_0_1
#include "hwcomposer_backend_v0.h"

HwComposerBackend_v0::HwComposerBackend_v0(hw_module_t *hwc_module, hw_device_t *hw_device, void *libminisf)
    : HwComposerBackend(hwc_module, libminisf)
    , hwc_device((hwc_composer_device_t *)hw_device)
    , hwc_layer_list(NULL)
{
    // Allocate hardware composer layer list
    hwc_layer_list = new hwc_layer_list_t();
    hwc_layer_list->flags = HWC_GEOMETRY_CHANGED;
    hwc_layer_list->numHwLayers = 0;
}

HwComposerBackend_v0::~HwComposerBackend_v0()
{
    if (hwc_layer_list != NULL) {
        delete hwc_layer_list;
    }

    // Close the hwcomposer handle
    HWC_PLUGIN_EXPECT_ZERO(hwc_close(hwc_device));
}

EGLNativeDisplayType
HwComposerBackend_v0::display()
{
    return EGL_DEFAULT_DISPLAY;
}

EGLNativeWindowType
HwComposerBackend_v0::createWindow(int width, int height)
{
    Q_UNUSED(width);
    Q_UNUSED(height);

    return (EGLNativeWindowType) NULL;
}

void
HwComposerBackend_v0::destroyWindow(EGLNativeWindowType window)
{
    Q_UNUSED(window);
}

void
HwComposerBackend_v0::swap(EGLNativeDisplayType display, EGLSurface surface)
{
    // TODO: Wait for vsync

    HWC_PLUGIN_EXPECT_ZERO(hwc_device->prepare(hwc_device, hwc_layer_list));
    HWC_PLUGIN_EXPECT_ZERO(hwc_device->set(hwc_device, display, surface, hwc_layer_list));
}

void
HwComposerBackend_v0::sleepDisplay(bool sleep)
{
    if (sleep) {
        HWC_PLUGIN_EXPECT_ZERO(hwc_device->set(hwc_device, NULL, NULL, NULL));
    } else {
        hwc_layer_list->flags = HWC_GEOMETRY_CHANGED;
    }
}

float
HwComposerBackend_v0::refreshRate()
{
    int vsyncVal = 0; // in ns

    int res = hwc_device->query(hwc_device, HWC_VSYNC_PERIOD, &vsyncVal);
    if (res != 0 || vsyncVal == 0) {
        qWarning() << "query(HWC_VSYNC_PERIOD) failed, assuming 60 Hz";
        return 60.0;
    }

    float fps = (float)1000000000 / (float)vsyncVal;
    qDebug("VSync: %dns, %ffps", vsyncVal, fps);
    return fps;
}
#endif
#endif
