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

#include "hwcomposer_backend_v11.h"

#ifdef HWC_PLUGIN_HAVE_HWCOMPOSER1_API

static int g_external_connected = 0;
static int g_external_connected_next = 0;
static int g_unblanked_displays[HWC_NUM_DISPLAY_TYPES] = { 0 };

class HwComposerBackendWindow_v11 : public HWComposerNativeWindow
{
public:
    HwComposerBackendWindow_v11(unsigned int width, unsigned int height,
            unsigned int format, HwComposerBackend_v11 *backend)
        : HWComposerNativeWindow(width, height, format)
        , backend(backend)
    {
    }

protected:
    void present(HWComposerNativeWindowBuffer *buffer)
    {
        RetireFencePool pool;

        // Obtain a new acquire fence to be used, then also
        // set the new release fence with the return value
        int fence = getFenceBufferFd(buffer);
        fence = backend->present(&pool, buffer->handle, fence);
        setFenceBufferFd(buffer, fence);

        // Retire fence pool will wait on and close all FDs consumed here
    }

private:
    HwComposerBackend_v11 *backend;
};


static void
hwcv11_proc_invalidate(const struct hwc_procs* procs)
{
    fprintf(stderr, "%s: procs=%x\n", __func__, procs);
}

static void
hwcv11_proc_vsync(const struct hwc_procs* procs, int disp, int64_t timestamp)
{
    fprintf(stderr, "%s: procs=%x, disp=%d, timestamp=%.0f\n", __func__, procs, disp, (float)timestamp);
}

static void
hwcv11_proc_hotplug(const struct hwc_procs* procs, int disp, int connected)
{
    fprintf(stderr, "%s: procs=%x, disp=%d, connected=%d\n", __func__, procs, disp, connected);
    if (disp == HWC_DISPLAY_EXTERNAL) {
        g_external_connected_next = connected;
    }
}

static hwc_procs_t hwcv11_procs = {
    hwcv11_proc_invalidate,
    hwcv11_proc_vsync,
    hwcv11_proc_hotplug,
};

static void
get_screen_size(hwc_composer_device_1_t *hwc_device, int id, int *width, int *height)
{
    size_t count = 1;
    uint32_t config = 0;
    if (hwc_device->getDisplayConfigs(hwc_device, id, &config, &count) == 0) {
        uint32_t attrs[] = {
            HWC_DISPLAY_WIDTH,
            HWC_DISPLAY_HEIGHT,
            HWC_DISPLAY_NO_ATTRIBUTE,
        };
        int32_t values[] = {
            0,
            0,
            0,
        };

        hwc_device->getDisplayAttributes(hwc_device, id, config, attrs, values);
        //fprintf(stderr, "Display %d size: %dx%d\n", id, values[0], values[1]);
        *width = values[0];
        *height = values[1];
    } else {
        //fprintf(stderr, "No size for display %d (not connected)\n", id);
    }
}

static void
dump_attributes(hwc_composer_device_1_t *hwc_device, int num_displays)
{
    // Get display configs
    for (int dpy=0; dpy<num_displays; dpy++) {
        size_t numConfigs = 32;
        uint32_t configs[numConfigs];
        if (hwc_device->getDisplayConfigs(hwc_device, dpy, configs, &numConfigs) != 0) {
            fprintf(stderr, "Display %d not connected, no configs\n", dpy);
            continue;
        }

        fprintf(stderr, "%d configs found for display %d\n", numConfigs, dpy);

        for (int i=0; i<numConfigs; i++) {
            uint32_t attributes[] = {
                HWC_DISPLAY_VSYNC_PERIOD,
                HWC_DISPLAY_WIDTH,
                HWC_DISPLAY_HEIGHT,
                HWC_DISPLAY_DPI_X,
                HWC_DISPLAY_DPI_Y,
                HWC_DISPLAY_NO_ATTRIBUTE, // sentinel
            };
            int32_t values[sizeof(attributes)/sizeof(attributes[0])];

            hwc_device->getDisplayAttributes(hwc_device, dpy, configs[i],
                    attributes, values);

            fprintf(stderr, "Dpy %d Cfg %d (%d) VSYNC_PERIOD=%d, SIZE=(%d, %d), DPY=(%d, %d)\n",
                    dpy, i, configs[i], values[0], values[1], values[2], values[3], values[4]);
        }
    }
}

// contents for a single screen
class HwComposerScreen_v11 {
public:
    enum Layer {
        // Layers we use for composition
        HWC_SCREEN_FRAMEBUFFER_LAYER = 0,
        HWC_SCREEN_FRAMEBUFFER_TARGET_LAYER = 1,

        // Number of layers we need to allocate space for
        HWC_SCREEN_REQUIRED_LAYERS = 2,
    };

    HwComposerScreen_v11(hwc_composer_device_1_t *hwc_device, int id)
        : hwc_device(hwc_device)
        , id(id)
        , hwc_list(nullptr)
    {
        size_t needed_size = sizeof(hwc_display_contents_1_t) +
            HWC_SCREEN_REQUIRED_LAYERS * sizeof(hwc_layer_1_t);

        hwc_list = (hwc_display_contents_1_t *) calloc(1, needed_size);

        // Need to set this here, and not every time in relayout
        hwc_list->numHwLayers = 2;
        hwc_list->retireFenceFd = -1;
    }

    bool relayout(int width, int height)
    {
        // Source rectangle of the desktop
        const hwc_rect_t source_rect = {
            0, 0, width, height
        };

        int ww = width, hh = height;
        get_screen_size(hwc_device, id, &ww, &hh);

        // Destination rectangle on the actual screen
        const hwc_rect_t dest_rect = {
            0, 0, ww, hh
        };

        hwc_layer_1_t *layer = NULL;

        layer = getLayer(HWC_SCREEN_FRAMEBUFFER_LAYER);
        resetLayer(layer);

        layer->compositionType = HWC_FRAMEBUFFER;
        layer->sourceCrop = source_rect;
        layer->displayFrame = dest_rect;
        layer->visibleRegionScreen.numRects = 1;
        layer->visibleRegionScreen.rects = &layer->displayFrame;

        layer = getLayer(HWC_SCREEN_FRAMEBUFFER_TARGET_LAYER);
        resetLayer(layer);

        layer->compositionType = HWC_FRAMEBUFFER_TARGET;
        layer->transform = (ww > hh) ? HWC_TRANSFORM_ROT_270 : 0; // FIXME: be more intelligent than "ww > hh"
        layer->sourceCrop = source_rect;
        layer->displayFrame = dest_rect;
        layer->visibleRegionScreen.numRects = 1;
        layer->visibleRegionScreen.rects = &layer->displayFrame;
        layer->acquireFenceFd = -1;
        layer->releaseFenceFd = -1;

        // For now, we always return true (=geometry has changed)
        return true;
    }

    ~HwComposerScreen_v11()
    {
        free(hwc_list);
    }

    hwc_display_contents_1_t *get()
    {
        return hwc_list;
    }

    void prepare(buffer_handle_t handle, int acquireFenceFd, bool geometryChanged)
    {
        trace_fds(__func__);

        hwc_layer_1_t *fblayer = getLayer(HWC_SCREEN_FRAMEBUFFER_TARGET_LAYER);

        fblayer->handle = handle;

        if (g_unblanked_displays[id]) {
            fprintf(stderr, "%s: dup'ing acquire fence (%d) for display %d\n", __func__,
                    acquireFenceFd, id);
            fblayer->acquireFenceFd = dup(acquireFenceFd);
        } else {
            fblayer->acquireFenceFd = -1;
        }

        fblayer->releaseFenceFd = -1;

        if (geometryChanged) {
            hwc_list->flags |= HWC_GEOMETRY_CHANGED;
        }
    }

    int release(int result)
    {
        trace_fds(__func__);

        // We assume the non-FB-target layer has its releaseFenceFd set to -1
        HWC_PLUGIN_EXPECT_ZERO(getLayer(HWC_SCREEN_FRAMEBUFFER_LAYER)->releaseFenceFd != -1);

        hwc_layer_1_t *fblayer = getLayer(HWC_SCREEN_FRAMEBUFFER_TARGET_LAYER);

        if (result == -1) {
            // we have found a fence to return
            result = fblayer->releaseFenceFd;
        } else {
            // additional fences need to be closed here
            if (fblayer->releaseFenceFd != -1) {
#if 0
                fprintf(stderr, "Merging release fences\n");
                result = sync_merge("qpa-hwc-merged", result, fblayer->releaseFenceFd);
#endif
                fprintf(stderr, "Closing release fence\n");
                //sync_wait(fblayer->releaseFenceFd, -1); // Need to wait, too?
                close(fblayer->releaseFenceFd);
            }

        }

        fblayer->releaseFenceFd = -1;

        return result;
    }

private: // functions
    void resetLayer(hwc_layer_1_t *layer)
    {
        layer->hints = 0;
        layer->flags = 0;
        layer->handle = 0;
        layer->transform = 0;

        layer->blending = HWC_BLENDING_NONE;

        layer->acquireFenceFd = -1;
        layer->releaseFenceFd = -1;
    }

    hwc_layer_1_t *getLayer(enum Layer layer)
    {
        return &hwc_list->hwLayers[layer];
    }

    void trace_fds(const char *func)
    {
        fprintf(stderr, "[%s] fds for dpy %d: FTa %d FTr %d FBa %d FBr %d\n",
                func, id,
                getLayer(HWC_SCREEN_FRAMEBUFFER_TARGET_LAYER)->acquireFenceFd,
                getLayer(HWC_SCREEN_FRAMEBUFFER_TARGET_LAYER)->releaseFenceFd,
                getLayer(HWC_SCREEN_FRAMEBUFFER_LAYER)->acquireFenceFd,
                getLayer(HWC_SCREEN_FRAMEBUFFER_LAYER)->releaseFenceFd);
    }

private: // members
    hwc_composer_device_1_t *hwc_device;
    int id;
    hwc_display_contents_1_t *hwc_list;
};


// collection of screens
class HwComposerContent_v11 {
public:
    HwComposerContent_v11(hwc_composer_device_1_t *hwc_device, int num_displays)
        : hwc_device(hwc_device)
        , num_displays(num_displays)
        , screens()
        , contents((hwc_display_contents_1_t **)calloc(num_displays,
                    sizeof(hwc_display_contents_1_t *)))
    {
        for (int i=0; i<num_displays; i++) {
            screens.push_back(new HwComposerScreen_v11(hwc_device, i));
        }
    }

    ~HwComposerContent_v11()
    {
        free(contents);

        for (auto &screen: screens) {
            delete screen;
        }
    }

    void prepare(buffer_handle_t handle, int acquireFenceFd, int width, int height, bool geometryChanged) {
        for (auto &screen: screens) {
            if (screen->relayout(width, height)) {
                geometryChanged = true;
            }

            screen->prepare(handle, acquireFenceFd, geometryChanged);
        }

        // We can close the acquire fence here, as we've dup'ed it for all displays here already
        //sync_wait(acquireFenceFd, -1);
        close(acquireFenceFd);
    }

    int release() {
        int result = -1;

        for (auto &screen: screens) {
            result = screen->release(result);
        }

        return result;
    }

    hwc_display_contents_1_t **get()
    {
        // Assemble content for all screens
        for (int i=0; i<num_displays; i++) {
            contents[i] = screens[i]->get();
        }

        return contents;
    }

private:
    hwc_composer_device_1_t *hwc_device;
    int num_displays;
    std::vector<HwComposerScreen_v11 *> screens;
    hwc_display_contents_1_t **contents;
};


HwComposerBackend_v11::HwComposerBackend_v11(hw_module_t *hwc_module, hw_device_t *hw_device, int num_displays)
    : HwComposerBackend(hwc_module)
    , hwc_device((hwc_composer_device_1_t *)hw_device)
    , hwc_win(NULL)
    , num_displays(num_displays)
    , width(0)
    , height(0)
    , content(new HwComposerContent_v11(hwc_device, num_displays))
    , display_sleeping(false)
{
    fprintf(stderr, "Registering hwc procs\n");
    hwc_device->registerProcs(hwc_device, &hwcv11_procs);
    sleepDisplay(false);
}

HwComposerBackend_v11::~HwComposerBackend_v11()
{
    // Destroy the window if it hasn't yet been destroyed
    if (hwc_win != NULL) {
        delete hwc_win;
    }

    // Destroy the content layout
    delete content;

    // Close the hwcomposer handle
    HWC_PLUGIN_EXPECT_ZERO(hwc_close_1(hwc_device));
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

    this->width = width;
    this->height = height;

    hwc_win = new HwComposerBackendWindow_v11(width, height, HAL_PIXEL_FORMAT_RGBA_8888, this);
    return (EGLNativeWindowType) static_cast<ANativeWindow *>(hwc_win);
}

void
HwComposerBackend_v11::destroyWindow(EGLNativeWindowType window)
{
    Q_UNUSED(window);

    // FIXME: Implement (delete hwc_win + set it to NULL?, also set size to 0x0)
}

void
HwComposerBackend_v11::swap(EGLNativeDisplayType display, EGLSurface surface)
{
    // TODO: Wait for vsync?

    HWC_PLUGIN_ASSERT_NOT_NULL(hwc_win);

    if (g_external_connected != g_external_connected_next) {
        g_external_connected = g_external_connected_next;

        // Force re-run of sleep display so that the external display is
        // powered on/off immediately (and not only after blank/unblank cycle)
        sleepDisplay(display_sleeping);
    }
    
    eglSwapBuffers(display, surface);
}

int
HwComposerBackend_v11::present(RetireFencePool *pool, buffer_handle_t handle, int acquireFenceFd)
{
    // Always force geometry change for now, later on
    // only do that on unblank/blank and attach/detach
    bool geometryChanged = true;

    content->prepare(handle, acquireFenceFd, width, height, geometryChanged);

    auto display_list = content->get();

    // Collect and reset all retire fence fds
    for (int i=0; i<num_displays; i++) {
        pool->consume(display_list[i]->retireFenceFd);
    }

    HWC_PLUGIN_EXPECT_ZERO(hwc_device->prepare(hwc_device, num_displays, display_list));
    HWC_PLUGIN_EXPECT_ZERO(hwc_device->set(hwc_device, num_displays, display_list));

    return content->release();
}

void
HwComposerBackend_v11::sleepDisplay(bool sleep)
{
    // XXX: For debugging only
    //dump_attributes(hwc_device, num_displays);

    if (sleep) {
        for (int i=0; i<num_displays; i++) {
            if (g_unblanked_displays[i]) {
                g_unblanked_displays[i] = (hwc_device->blank(hwc_device, i, 1) != 0);
            }
        }
    } else {
#if 0
        for (int i=0; i<num_displays; i++) {
            g_unblanked_displays[i] = (hwc_device->blank(hwc_device, i, 0) == 0);
        }
#endif

        if (g_external_connected) {
            if (g_unblanked_displays[0]) {
                fprintf(stderr, "Blanking internal display\n");
                g_unblanked_displays[0] = (hwc_device->blank(hwc_device, 0, 1) != 0);
            }
            fprintf(stderr, "Unblanking external display\n");
            g_unblanked_displays[1] = (hwc_device->blank(hwc_device, 1, 0) == 0);
        } else {
            if (g_unblanked_displays[1]) {
                fprintf(stderr, "Blanking external display\n");
                g_unblanked_displays[1] = (hwc_device->blank(hwc_device, 1, 1) != 0);
            }
            fprintf(stderr, "Unblanking internal display\n");
            g_unblanked_displays[0] = (hwc_device->blank(hwc_device, 0, 0) == 0);
        }

        // TODO: Force geometry change
    }

    display_sleeping = sleep;
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
