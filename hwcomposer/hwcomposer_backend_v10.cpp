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

#include <inttypes.h>
#include <unistd.h>

#ifdef HWC_DEVICE_API_VERSION_1_0

/* For vsync thread synchronization */
static QMutex vsync_mutex;
static QWaitCondition vsync_cond;

static float vsyncFPS = -1;

const char *
comp_type_str(int32_t type)
{
    switch (type) {
        case HWC_BACKGROUND: return "BACKGROUND";
        case HWC_FRAMEBUFFER_TARGET: return "FB TARGET";
        case HWC_FRAMEBUFFER: return "FB";
        case HWC_OVERLAY: return "OVERLAY";
    }

    return "unknown";
}

const char *
blending_type_str(int32_t type)
{
    switch (type) {
        case HWC_BLENDING_NONE: return "NONE";
        case HWC_BLENDING_PREMULT: return "PREMULT";
        case HWC_BLENDING_COVERAGE: return "COVERAGE";
    }

    return "unknown";
}

static void
dump_display_contents(hwc_display_contents_1_t *contents)
{
    static const char *dump_env = getenv("HWC_DUMP_DISPLAY_CONTENTS");
    static bool do_dump = (dump_env != NULL && strcmp(dump_env, "1") == 0);

    if (!do_dump) {
        return;
    }

    fprintf(stderr, "============ QPA-HWC: dump_display_contents(%p) ============\n",  contents);
    fprintf(stderr, "retireFenceFd = %d\n", contents->retireFenceFd);
    fprintf(stderr, "dpy = %p\n", contents->dpy);
    fprintf(stderr, "sur = %p\n", contents->sur);
    fprintf(stderr, "flags = %x\n", contents->flags);
    fprintf(stderr, "numHwLayers = %zu\n", contents->numHwLayers);
    for (unsigned int i=0; i<contents->numHwLayers; i++) {
        hwc_layer_1_t *layer = &(contents->hwLayers[i]);
        fprintf(stderr, "Layer %d (%p):\n"
                        "    type=%s, hints=%x, flags=%x, handle=%" PRIxPTR ", transform=%d, blending=%s\n"
                        "    sourceCrop={%d, %d, %d, %d}, displayFrame={%d, %d, %d, %d}\n"
                        "    visibleRegionScreen=<%zu rect(s)>, acquireFenceFd=%d, releaseFenceFd=%d\n",
                i, layer, comp_type_str(layer->compositionType), layer->hints, layer->flags, (uintptr_t)layer->handle,
                layer->transform, blending_type_str(layer->blending),
                layer->sourceCrop.left, layer->sourceCrop.top, layer->sourceCrop.right, layer->sourceCrop.bottom,
                layer->displayFrame.left, layer->displayFrame.top, layer->displayFrame.right, layer->displayFrame.bottom,
                layer->visibleRegionScreen.numRects, layer->acquireFenceFd, layer->releaseFenceFd);
    }
}

void
hwcv10_proc_invalidate(const struct hwc_procs* procs)
{
    fprintf(stderr, "%s: procs=%" PRIxPTR "\n", __func__, (uintptr_t)procs);
}

void
hwcv10_proc_vsync(const struct hwc_procs* /*procs*/, int /*disp*/, int64_t /*timestamp*/)
{
    //fprintf(stderr, "%s: procs=%x, disp=%d, timestamp=%.0f\n", __func__, procs, disp, (float)timestamp);
    vsync_mutex.lock();
    vsync_cond.wakeOne();
    vsync_mutex.unlock();
}

void
hwcv10_proc_hotplug(const struct hwc_procs* procs, int disp, int connected)
{
    fprintf(stderr, "%s: procs=%" PRIxPTR ", disp=%d, connected=%d\n", __func__, (uintptr_t)procs, disp, connected);
}

static hwc_procs_t global_procs = {
    hwcv10_proc_invalidate,
    hwcv10_proc_vsync,
    hwcv10_proc_hotplug,
};


HwComposerBackend_v10::HwComposerBackend_v10(hw_module_t *hwc_module, hw_device_t *hw_device, void *libminisf)
    : HwComposerBackend(hwc_module, libminisf)
    , hwc_device((hwc_composer_device_1_t *)hw_device)
    , hwc_list(NULL)
    , hwc_mList(NULL)
    , hwc_numDisplays(1) // "For HWC 1.0, numDisplays will always be one."
{
    hwc_device->registerProcs(hwc_device, &global_procs);
    hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 1);
    sleepDisplay(false);
}

HwComposerBackend_v10::~HwComposerBackend_v10()
{
    hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 0);

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

    // Number of hardware layers we want (right now, only one rendered via GLES)
    int numHwLayers = 1;

    // Display contents list
    size_t required = sizeof(hwc_display_contents_1_t) + numHwLayers * sizeof(hwc_layer_1_t);
    hwc_list = (hwc_display_contents_1_t *) calloc(1, required);
    hwc_list->retireFenceFd = -1;
    hwc_list->flags = HWC_GEOMETRY_CHANGED;
    hwc_list->numHwLayers = numHwLayers;

    hwc_rect_t r = { 0, 0, width, height };

    hwc_layer_1_t *layer = &(hwc_list->hwLayers[0]);
    layer->compositionType = HWC_FRAMEBUFFER;
    layer->hints = 0;
    layer->flags = HWC_SKIP_LAYER;
    layer->handle = NULL;
    layer->transform = 0;
    layer->blending = HWC_BLENDING_NONE;
    layer->sourceCrop = r;
    layer->displayFrame = r;
    layer->visibleRegionScreen.numRects = 1;
    layer->visibleRegionScreen.rects = &layer->displayFrame;
    layer->acquireFenceFd = -1;
    layer->releaseFenceFd = -1;

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
HwComposerBackend_v10::swap(EGLNativeDisplayType /*display*/, EGLSurface /*surface*/)
{
    HWC_PLUGIN_ASSERT_ZERO(!(hwc_list->retireFenceFd == -1));

    // Wait for vsync before posting new frame
    // or force swap if exceeding the vsync timeframe
    vsync_mutex.lock();
    vsync_cond.wait(&vsync_mutex, 1000/vsyncFPS);
    vsync_mutex.unlock();

    hwc_list->dpy = EGL_NO_DISPLAY;
    hwc_list->sur = EGL_NO_SURFACE;
    HWC_PLUGIN_ASSERT_ZERO(hwc_device->prepare(hwc_device, hwc_numDisplays, hwc_mList));

    // (dpy, sur) is the target of SurfaceFlinger's OpenGL ES composition for
    // HWC_DEVICE_VERSION_1_0. They aren't relevant to prepare. The set call
    // should commit this surface atomically to the display along with any
    // overlay layers.
    hwc_list->dpy = eglGetCurrentDisplay();
    hwc_list->sur = eglGetCurrentSurface(EGL_DRAW);
    dump_display_contents(hwc_list);
    HWC_PLUGIN_ASSERT_ZERO(hwc_device->set(hwc_device, hwc_numDisplays, hwc_mList));

    if (hwc_list->retireFenceFd != -1) {
        sync_wait(hwc_list->retireFenceFd, -1);
        close(hwc_list->retireFenceFd);
        hwc_list->retireFenceFd = -1;
    }
}

void
HwComposerBackend_v10::sleepDisplay(bool sleep)
{
    if (sleep) {
        HWC_PLUGIN_EXPECT_ZERO(hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 0));
        HWC_PLUGIN_EXPECT_ZERO(hwc_device->blank(hwc_device, 0, 1));
    }
    else {
        HWC_PLUGIN_EXPECT_ZERO(hwc_device->blank(hwc_device, 0, 0));
        HWC_PLUGIN_EXPECT_ZERO(hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 1));
    }

    if (!sleep && hwc_list != NULL) {
        hwc_list->flags = HWC_GEOMETRY_CHANGED;
    }
}

float
HwComposerBackend_v10::refreshRate()
{
    if(vsyncFPS == -1) {
        int vsyncVal = 0; // in ns

        int res = hwc_device->query(hwc_device, HWC_VSYNC_PERIOD, &vsyncVal);
        if (res != 0 || vsyncVal == 0) {
            qWarning() << "query(HWC_VSYNC_PERIOD) failed, assuming 60 Hz";
            vsyncVal = 60.0;
        }

        vsyncFPS = (float)1000000000 / (float)vsyncVal;
        qDebug("VSync: %dns, %ffps", vsyncVal, vsyncFPS);
    }
    return vsyncFPS;
}

#endif /* HWC_DEVICE_API_VERSION_1_0 */
