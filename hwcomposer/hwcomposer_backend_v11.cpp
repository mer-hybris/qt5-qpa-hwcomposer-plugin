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

#include "hwcomposer_backend_v11.h"
#include <stdio.h>

#ifdef HWC_PLUGIN_HAVE_HWCOMPOSER1_API

class HWComposer : public HWComposerNativeWindow
{
    private:
        hwc_layer_1_t *fblayer;
        hwc_composer_device_1_t *hwcdevice;
        hwc_display_contents_1_t **mlist;
        int num_displays;
    protected:
        void present(HWComposerNativeWindowBuffer *buffer);

    public:

    HWComposer(unsigned int width, unsigned int height, unsigned int format,
            hwc_composer_device_1_t *device, hwc_display_contents_1_t **mList,
            hwc_layer_1_t *layer, int num_displays);
    void set();
};

HWComposer::HWComposer(unsigned int width, unsigned int height, unsigned int format,
        hwc_composer_device_1_t *device, hwc_display_contents_1_t **mList,
        hwc_layer_1_t *layer, int num_displays)
    : HWComposerNativeWindow(width, height, format)
    , fblayer(layer)
    , hwcdevice(device)
    , mlist(mList)
    , num_displays(num_displays)
{
}

void HWComposer::present(HWComposerNativeWindowBuffer *buffer)
{
    int oldretire = mlist[0]->retireFenceFd;
    mlist[0]->retireFenceFd = -1;
    mlist[0]->numHwLayers = 1;
    mlist[0]->hwLayers[0].compositionType = HWC_FRAMEBUFFER_TARGET;
    mlist[0]->hwLayers[0].handle = buffer->handle;
    mlist[0]->hwLayers[0].acquireFenceFd = getFenceBufferFd(buffer);
    mlist[0]->hwLayers[0].releaseFenceFd = -1;

    int err = hwcdevice->prepare(hwcdevice, num_displays, mlist);
    HWC_PLUGIN_EXPECT_ZERO(err);
    err = hwcdevice->set(hwcdevice, num_displays, mlist);
    HWC_PLUGIN_EXPECT_ZERO(err);
    setFenceBufferFd(buffer, mlist[0]->hwLayers[0].releaseFenceFd);

    if (oldretire != -1)
    {
        sync_wait(oldretire, -1);
        close(oldretire);
    }
}

static const uint32_t DISPLAY_ATTRIBUTES[] = {
    HWC_DISPLAY_VSYNC_PERIOD,
    HWC_DISPLAY_WIDTH,
    HWC_DISPLAY_HEIGHT,
    HWC_DISPLAY_DPI_X,
    HWC_DISPLAY_DPI_Y,
    HWC_DISPLAY_NO_ATTRIBUTE,
};
#define NUM_DISPLAY_ATTRIBUTES (sizeof(DISPLAY_ATTRIBUTES) / sizeof(DISPLAY_ATTRIBUTES)[0])

struct callbacks : public hwc_procs_t {
       // these are here to facilitate the transition when adding
        //         // new callbacks (an implementation can check for NULL before
        //                 // calling a new callback).
                     void (*zero[4])(void);
       HwComposerBackend_v11 *hwc;
} hwc_callbacks;

void hook_invalidate(const struct hwc_procs* procs) {
     fprintf(stderr, "=== invalidate hook called\n");
#if 0
     reinterpret_cast<const struct callbacks *>(procs)->hwc->invalidate();
#endif
}

void hook_vsync(const struct hwc_procs* procs, int disp,
        int64_t timestamp) {
    fprintf(stderr, "=== vsync %i %i called\n", disp, timestamp);
#if 0
    reinterpret_cast<const struct callbacks *>(procs)->hwc->vsync(disp, timestamp);
#endif
}

void hook_hotplug(const struct hwc_procs* procs, int disp,
        int connected) {
    fprintf(stderr, "=== hotplug %i %i\n", disp, connected);
#if 0
    reinterpret_cast<const struct callbacks *>(procs)->hwc->hotplug(disp, connected);
#endif
}

void HwComposerBackend_v11::invalidate()
{
}

void HwComposerBackend_v11::vsync(int disp, int64_t timestamp)
{
}

void HwComposerBackend_v11::hotplug(int disp, int connected)
{
}

HwComposerBackend_v11::HwComposerBackend_v11(hw_module_t *hwc_module, hw_device_t *hw_device, int num_displays)
    : HwComposerBackend(hwc_module)
    , hwc_device((hwc_composer_device_1_t *)hw_device)
    , hwc_win(NULL)
    , hwc_list(NULL)
    , hwc_mList(NULL)
    , oldretire(-1)
    , oldrelease(-1)
    , oldrelease2(-1)
    , num_displays(num_displays)
{
    int disp = 0;

    hwc_callbacks.invalidate = &hook_invalidate;
    hwc_callbacks.vsync = &hook_vsync;
    hwc_callbacks.hotplug = &hook_hotplug;
    hwc_callbacks.hwc = this;

    hwc_device->registerProcs(hwc_device, &hwc_callbacks);
    hwc_device->eventControl(hwc_device, HWC_DISPLAY_PRIMARY, HWC_EVENT_VSYNC, 0);
    for (disp = 0; disp < num_displays; disp++)
    {

	    uint32_t config;
	    int32_t values[NUM_DISPLAY_ATTRIBUTES - 1];
	    size_t numConfigs = 1;
	    int err = hwc_device->getDisplayConfigs(hwc_device, disp, &config, &numConfigs);
	    if (err != 0) {
		 fprintf(stderr, "== disp %i offline\n", disp);
	    }
            err = hwc_device->getDisplayAttributes(hwc_device, disp, config, DISPLAY_ATTRIBUTES, values);
	    if (err != 0)
            {
                  fprintf(stderr, "== disp %i unable to get attributes\n", disp);
	    }
    }

    sleepDisplay(false);
}

HwComposerBackend_v11::~HwComposerBackend_v11()
{
    // Destroy the window if it hasn't yet been destroyed
    if (hwc_win != NULL) {
        delete hwc_win;
    }

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
HwComposerBackend_v11::display()
{
    return EGL_DEFAULT_DISPLAY;
}

EGLNativeWindowType
HwComposerBackend_v11::createWindow(int width, int height)
{
    // We expect that we haven't created a window already, if we had, we
    // would leak stuff, and we want to avoid that for obvious reasons.
    HWC_PLUGIN_EXPECT_NULL(hwc_win);
    HWC_PLUGIN_EXPECT_NULL(hwc_list);
    HWC_PLUGIN_EXPECT_NULL(hwc_mList);

    size_t neededsize = sizeof(hwc_display_contents_1_t) + 1 * sizeof(hwc_layer_1_t);
    hwc_list = (hwc_display_contents_1_t *) malloc(neededsize);
    hwc_mList = (hwc_display_contents_1_t **) malloc(num_displays * sizeof(hwc_display_contents_1_t *));
    const hwc_rect_t r = { 0, 0, width, height };

    hwc_mList[0] = hwc_list;
    for (int i = 1; i < num_displays; i++)
	hwc_mList[i] = NULL;

    hwc_layer_1_t *layer = NULL;

    layer = &hwc_list->hwLayers[0];
    memset(layer, 0, sizeof(hwc_layer_1_t));
    layer->compositionType = HWC_FRAMEBUFFER_TARGET;
    layer->hints = 0;
    layer->flags = 0;
    layer->handle = 0;
    layer->transform = 0;
    layer->blending = HWC_BLENDING_NONE;
    layer->sourceCrop = r;
#ifdef HWC_DEVICE_API_VERSION_1_3
    layer->sourceCropf.top = 0.0f;
    layer->sourceCropf.left = 0.0f;
    layer->sourceCropf.bottom = (float) height;
    layer->sourceCropf.right = (float) width;
#endif
    layer->displayFrame = r;
    layer->visibleRegionScreen.numRects = 1;
    layer->visibleRegionScreen.rects = &layer->displayFrame;
    layer->acquireFenceFd = -1;
    layer->releaseFenceFd = -1;
#if (ANDROID_VERSION_MAJOR >= 4) && (ANDROID_VERSION_MINOR >= 3)
    layer->planeAlpha = 0xff;
#endif

    hwc_list->retireFenceFd = -1;
    hwc_list->flags = HWC_GEOMETRY_CHANGED;
    hwc_list->numHwLayers = 1;

    hwc_win = new HWComposer(width, height, HAL_PIXEL_FORMAT_RGBA_8888,
            hwc_device, hwc_mList, &hwc_list->hwLayers[0], num_displays);
    return (EGLNativeWindowType) static_cast<ANativeWindow *>(hwc_win);
}

void
HwComposerBackend_v11::destroyWindow(EGLNativeWindowType window)
{
    Q_UNUSED(window);

    // FIXME: Implement (delete hwc_win + set it to NULL?)
}

void
HwComposerBackend_v11::swap(EGLNativeDisplayType display, EGLSurface surface)
{
    // TODO: Wait for vsync?

    HWC_PLUGIN_ASSERT_NOT_NULL(hwc_win);

    eglSwapBuffers(display, surface);
}

void
HwComposerBackend_v11::sleepDisplay(bool sleep)
{
    if (sleep) {
        HWC_PLUGIN_EXPECT_ZERO(hwc_device->blank(hwc_device, 0, 1));
    } else {
        HWC_PLUGIN_EXPECT_ZERO(hwc_device->blank(hwc_device, 0, 0));

        if (hwc_list) {
            hwc_list->flags |= HWC_GEOMETRY_CHANGED;
        }
    }
}

float
HwComposerBackend_v11::refreshRate()
{
    // TODO: Implement new hwc 1.1 querying of vsync period per-display
    //
    // from hwcomposer_defs.h:
    // "This query is not used for HWC_DEVICE_API_VERSION_1_1 and later.
    //  Instead, the per-display attribute HWC_DISPLAY_VSYNC_PERIOD is used."
    return 60.0;
}

#endif /* HWC_PLUGIN_HAVE_HWCOMPOSER1_API */
