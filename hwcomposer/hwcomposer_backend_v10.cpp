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

#include "hwcomposer_backend_v10.h"

HwComposerBackend_v10::HwComposerBackend_v10(hw_module_t *hwc_module, hw_device_t *hw_device)
    : HwComposerBackend(hwc_module)
    , hwc_device((hwc_composer_device_1_t *)hw_device)
    , hwc_list(NULL)
    , hwc_mList(NULL)
    , hwc_numDisplays(1) // "For HWC 1.0, numDisplays will always be one."
{
    HWC_PLUGIN_EXPECT_ZERO(hwc_device->blank(hwc_device, 0, 0));
}

HwComposerBackend_v10::~HwComposerBackend_v10()
{
    // Close the hwcomposer handle
    HWC_PLUGIN_EXPECT_ZERO(hwc_close_1(hwc_device));

    if (hwc_mList != NULL) {
        free(hwc_mList);
    }

    if (hwc_list != NULL) {
        free(hwc_list);
    }
}

EGLNativeDisplayType
HwComposerBackend_v10::display()
{
    return EGL_DEFAULT_DISPLAY;
}

EGLNativeWindowType
HwComposerBackend_v10::createWindow(int width, int height)
{
    // We expect that we haven't created a window already, if we had, we
    // would leak stuff, and we want to avoid that for obvious reasons.
    HWC_PLUGIN_EXPECT_NULL(hwc_list);
    HWC_PLUGIN_EXPECT_NULL(hwc_mList);

    // Display contents list (XXX: we have 0 layers, probably don't need to allocate space for them)
    hwc_list = (hwc_display_contents_1_t *) calloc(1, sizeof(hwc_display_contents_1_t) + 2 * sizeof(hwc_layer_1_t));
    hwc_list->retireFenceFd = -1;
    hwc_list->flags = HWC_GEOMETRY_CHANGED;
    hwc_list->numHwLayers = 0;

    // A list of display contents pointers for each display
    hwc_mList = (hwc_display_contents_1_t **) calloc(hwc_numDisplays, sizeof(hwc_display_contents_1_t *));
    for (int i = 0; i < hwc_numDisplays; i++) {
         hwc_mList[i] = hwc_list;
    }

    return (EGLNativeWindowType) NULL;
}

void
HwComposerBackend_v10::destroyWindow(EGLNativeWindowType window)
{
    Q_UNUSED(window);
}

void
HwComposerBackend_v10::swap(EGLNativeDisplayType display, EGLSurface surface)
{
    int oldretire = hwc_list->retireFenceFd;

    hwc_list->dpy = EGL_NO_DISPLAY;
    hwc_list->sur = EGL_NO_SURFACE;
    HWC_PLUGIN_ASSERT_ZERO(hwc_device->prepare(hwc_device, hwc_numDisplays, hwc_mList));

    // (dpy, sur) is the target of SurfaceFlinger's OpenGL ES composition for
    // HWC_DEVICE_VERSION_1_0. They aren't relevant to prepare. The set call
    // should commit this surface atomically to the display along with any
    // overlay layers.
    hwc_list->dpy = eglGetCurrentDisplay();
    hwc_list->sur = eglGetCurrentSurface(EGL_DRAW);
    HWC_PLUGIN_ASSERT_ZERO(hwc_device->set(hwc_device, hwc_numDisplays, hwc_mList));

    if (hwc_list->retireFenceFd != -1) {
        // XXX sync_wait(hwc_list->retireFenceFd, -1);
        close(hwc_list->retireFenceFd);
        hwc_list->retireFenceFd = -1;
    }
    hwc_list->flags &= ~HWC_GEOMETRY_CHANGED;
}

void
HwComposerBackend_v10::sleepDisplay(bool sleep)
{
    if (sleep) {
        HWC_PLUGIN_EXPECT_ZERO(hwc_device->set(hwc_device, NULL, NULL, NULL));
    } else {
        hwc_list->flags = HWC_GEOMETRY_CHANGED;
    }
}

float
HwComposerBackend_v10::refreshRate()
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
