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
#include "qeglfswindow.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QTimerEvent>
#include <QtCore/QCoreApplication>
#include <private/qwindow_p.h>

#include "qsystrace_selector.h"

#ifdef HWC_PLUGIN_HAVE_HWCOMPOSER1_API

// #define QPA_HWC_TIMING

#ifdef QPA_HWC_TIMING
#define QPA_HWC_TIMING_SAMPLE(variable) variable = timer.nsecsElapsed()
static QElapsedTimer timer;
static qint64 presentTime;
static qint64 syncTime;
static qint64 prepareTime;
static qint64 setTime;
#else
#define QPA_HWC_TIMING_SAMPLE(variable)
#endif


class HwComposerContent_v11;

static int g_external_connected = 0;
static int g_external_connected_next = 0;
static int g_unblanked_displays[HWC_NUM_DISPLAY_TYPES] = { 0 };

struct HwcProcs_v11 : public hwc_procs
{
    HwComposerBackend_v11 *backend;
    HwComposerContent_v11 *content;
};

static void hwc11_callback_vsync(const struct hwc_procs *procs, int, int64_t)
{
    static int counter = 0;
    ++counter;
    if (counter % 2)
        QSystrace::begin("graphics", "QPA::vsync", "");
    else
        QSystrace::end("graphics", "QPA::vsync", "");

    QCoreApplication::postEvent(static_cast<const HwcProcs_v11 *>(procs)->backend, new QEvent(QEvent::User));
}

static void hwc11_callback_invalidate(const struct hwc_procs *)
{
}

static void hwc11_callback_hotplug(const struct hwc_procs *procs, int disp, int connected);

class HwComposerBackendWindow_v11 : public HWComposerNativeWindow
{
public:
    HwComposerBackendWindow_v11(unsigned int width, unsigned int height,
            unsigned int format, HwComposerBackend_v11 *backend)
        : HWComposerNativeWindow(width, height, format)
        , backend(backend)
    {
        int bufferCount = qBound(2, qgetenv("QPA_HWC_BUFFER_COUNT").toInt(), 8);
        setBufferCount(bufferCount);
        m_syncBeforeSet = qEnvironmentVariableIsSet("QPA_HWC_SYNC_BEFORE_SET");
        m_waitOnRetireFence = qEnvironmentVariableIsSet("QPA_HWC_WAIT_ON_RETIRE_FENCE");
    }

protected:
    void present(HWComposerNativeWindowBuffer *buffer)
    {
        QSystraceEvent trace("graphics", "QPA::present");

        QPA_HWC_TIMING_SAMPLE(presentTime);

        RetireFencePool pool(m_waitOnRetireFence);

        // Obtain a new acquire fence to be used, then also
        // set the new release fence with the return value
        int fence = -1;
        if (m_syncBeforeSet) {
            int acqFd = getFenceBufferFd(buffer);
            if (acqFd >= 0) {
                sync_wait(acqFd, -1);
                close(acqFd);
                fence = -1;
            }
        } else {
            fence = getFenceBufferFd(buffer);
        }
        fence = backend->present(&pool, buffer->handle, fence);
        setFenceBufferFd(buffer, fence);

        // Retire fence pool will wait on and close all FDs consumed here
    }

private:
    HwComposerBackend_v11 *backend;
    bool m_syncBeforeSet;
    bool m_waitOnRetireFence;
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

        for (uint i=0; i<numConfigs; i++) {
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
        , m_screen_width(0)
        , m_screen_height(0)
    {
        size_t needed_size = sizeof(hwc_display_contents_1_t) +
            HWC_SCREEN_REQUIRED_LAYERS * sizeof(hwc_layer_1_t);

        hwc_list = (hwc_display_contents_1_t *) calloc(1, needed_size);

        // Need to set this here, and not every time in relayout
        hwc_list->numHwLayers = 2;
        hwc_list->retireFenceFd = -1;
#ifdef HWC_DEVICE_API_VERSION_1_3
        hwc_list->outbuf = 0;
        hwc_list->outbufAcquireFenceFd = -1;
#endif
    }

    void update_size() {
        get_screen_size(hwc_device, id, &m_screen_width, &m_screen_height);
    }

    bool relayout(int width, int height)
    {
        // Source rectangle of the desktop
        const hwc_rect_t source_rect = {
            0, 0, width, height
        };

        // Don't call getDisplayAttributes too often here since it can cause big slowdowns.
        if (m_screen_width <= 0 || m_screen_height <= 0) {
            update_size();
        }

        // Destination rectangle on the actual screen
        const hwc_rect_t dest_rect = {
            0, 0, m_screen_width, m_screen_height
        };

        hwc_layer_1_t *layer = NULL;

        layer = getLayer(HWC_SCREEN_FRAMEBUFFER_LAYER);
        resetLayer(layer);

        layer->compositionType = HWC_FRAMEBUFFER;
        layer->hints = 0;
        layer->flags = 0;
        layer->handle = 0;
        layer->transform = 0;
        layer->blending = HWC_BLENDING_NONE;
    #ifdef HWC_DEVICE_API_VERSION_1_3
        layer->sourceCropf.top = 0.0f;
        layer->sourceCropf.left = 0.0f;
        layer->sourceCropf.bottom = (float) height;
        layer->sourceCropf.right = (float) width;
    #else
        layer->sourceCrop = source_rect;
    #endif
        layer->displayFrame = dest_rect;
        layer->visibleRegionScreen.numRects = 1;
        layer->visibleRegionScreen.rects = &layer->displayFrame;
        layer->acquireFenceFd = -1;
        layer->releaseFenceFd = -1;
    #if (ANDROID_VERSION_MAJOR >= 4) && (ANDROID_VERSION_MINOR >= 3) || (ANDROID_VERSION_MAJOR >= 5)
        // We've observed that qualcomm chipsets enters into compositionType == 6
        // (HWC_BLIT), an undocumented composition type which gives us rendering
        // glitches and warnings in logcat. By setting the planarAlpha to non-
        // opaque, we attempt to force the HWC into using HWC_FRAMEBUFFER for this
        // layer so the HWC_FRAMEBUFFER_TARGET layer actually gets used.
        bool tryToForceGLES = !qgetenv("QPA_HWC_FORCE_GLES").isEmpty();
        layer->planeAlpha = tryToForceGLES ? 1 : 255;
    #endif
    #ifdef HWC_DEVICE_API_VERSION_1_5
        layer->surfaceDamage.numRects = 0;
    #endif

        layer = getLayer(HWC_SCREEN_FRAMEBUFFER_TARGET_LAYER);
        resetLayer(layer);

        layer->compositionType = HWC_FRAMEBUFFER_TARGET;
        layer->hints = 0;
        layer->flags = 0;
        layer->handle = 0;
        layer->transform = 0;
        layer->blending = HWC_BLENDING_NONE;
    #ifdef HWC_DEVICE_API_VERSION_1_3
        layer->sourceCropf.top = 0.0f;
        layer->sourceCropf.left = 0.0f;
        layer->sourceCropf.bottom = (float) height;
        layer->sourceCropf.right = (float) width;
    #else
        layer->sourceCrop = source_rect;
    #endif
        layer->displayFrame = dest_rect;
        layer->visibleRegionScreen.numRects = 1;
        layer->visibleRegionScreen.rects = &layer->displayFrame;
        layer->acquireFenceFd = -1;
        layer->releaseFenceFd = -1;
    #if (ANDROID_VERSION_MAJOR >= 4) && (ANDROID_VERSION_MINOR >= 3) || (ANDROID_VERSION_MAJOR >= 5)
        layer->planeAlpha = 0xff;
    #endif
    #ifdef HWC_DEVICE_API_VERSION_1_5
        layer->surfaceDamage.numRects = 0;
    #endif

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
        //trace_fds(__func__);

        hwc_layer_1_t *fblayer = getLayer(HWC_SCREEN_FRAMEBUFFER_TARGET_LAYER);

        fblayer->handle = handle;

        if (g_unblanked_displays[id]) {
            //fprintf(stderr, "%s: dup'ing acquire fence (%d) for display %d\n", __func__,
                    //acquireFenceFd, id);
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
        //trace_fds(__func__);

        // We assume the non-FB-target layer has its releaseFenceFd set to -1
        // HWC_PLUGIN_EXPECT_ZERO(getLayer(HWC_SCREEN_FRAMEBUFFER_LAYER)->releaseFenceFd != -1);
        // Unfortunately that doesn't work on certain devices.
        int f = getLayer(HWC_SCREEN_FRAMEBUFFER_LAYER)->releaseFenceFd;
        if (f != -1) close(f);
        getLayer(HWC_SCREEN_FRAMEBUFFER_LAYER)->releaseFenceFd = -1;

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
                //fprintf(stderr, "Closing release fence\n");
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
        memset(layer, 0, sizeof(hwc_layer_1_t));

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
    int m_screen_width;
    int m_screen_height;
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

    void update_screen_sizes() {
        for (auto &screen: screens) {
            screen->update_size();
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

HwComposerBackend_v11::HwComposerBackend_v11(hw_module_t *hwc_module, hw_device_t *hw_device, void *libminisf, int num_displays)
    : HwComposerBackend(hwc_module, libminisf)
    , hwc_device((hwc_composer_device_1_t *)hw_device)
    , hwc_list(NULL)
    , hwc_mList(NULL)
    , num_displays(num_displays)
    , m_displayOff(true)
    , width(0)
    , height(0)
    , content(new HwComposerContent_v11(hwc_device, num_displays))
{
    procs = new HwcProcs_v11();
    procs->invalidate = hwc11_callback_invalidate;
    procs->hotplug = hwc11_callback_hotplug;
    procs->vsync = hwc11_callback_vsync;
    procs->backend = this;
    procs->content = content;

    hwc_device->registerProcs(hwc_device, procs);

    hwc_version = interpreted_version(hw_device);
    sleepDisplay(false);
}

static void hwc11_callback_hotplug(const struct hwc_procs *procs, int disp, int connected)
{
    fprintf(stderr, "%s: procs=%x, disp=%d, connected=%d\n", __func__, procs, disp, connected);
    if (disp == HWC_DISPLAY_EXTERNAL) {
        g_external_connected_next = connected;
        ((struct HwcProcs_v11*)procs)->content->update_screen_sizes();
    }
}

HwComposerBackend_v11::~HwComposerBackend_v11()
{
    hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 0);

    // Destroy the content layout
    delete content;

    // Close the hwcomposer handle
    if (!qgetenv("QPA_HWC_WORKAROUNDS").split(',').contains("no-close-hwc"))
        HWC_PLUGIN_EXPECT_ZERO(hwc_close_1(hwc_device));

    if (hwc_mList != NULL) {
        free(hwc_mList);
    }

    if (hwc_list != NULL) {
        free(hwc_list);
    }

    delete procs;
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
    HWC_PLUGIN_EXPECT_NULL(hwc_list);
    HWC_PLUGIN_EXPECT_NULL(hwc_mList);


    this->width = width;
    this->height = height;

    HwComposerBackendWindow_v11 *hwc_win = new HwComposerBackendWindow_v11(width, height, HAL_PIXEL_FORMAT_RGBA_8888, this);
    return (EGLNativeWindowType) static_cast<ANativeWindow *>(hwc_win);
}

void
HwComposerBackend_v11::destroyWindow(EGLNativeWindowType window)
{
    Q_UNUSED(window);
}

void
HwComposerBackend_v11::swap(EGLNativeDisplayType display, EGLSurface surface)
{
#ifdef QPA_HWC_TIMING
    timer.start();
#endif

    if (g_external_connected != g_external_connected_next) {
        g_external_connected = g_external_connected_next;

        // Force re-run of sleep display so that the external display is
        // powered on/off immediately (and not only after blank/unblank cycle)
        sleepDisplay(m_displayOff);
    }

    eglSwapBuffers(display, surface);

#ifdef QPA_HWC_TIMING
    qDebug("HWComposerBackend::swap(), present=%.3f, sync=%.3f, prepare=%.3f, set=%.3f, total=%.3f",
           presentTime / 1000000.0,
           (syncTime - presentTime) / 1000000.0,
           (prepareTime - syncTime) / 1000000.0,
           (setTime - prepareTime) / 1000000.0,
           timer.nsecsElapsed() / 1000000.0);
#endif
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

    QPA_HWC_TIMING_SAMPLE(syncTime);

    HWC_PLUGIN_EXPECT_ZERO(hwc_device->prepare(hwc_device, num_displays, display_list));

    QPA_HWC_TIMING_SAMPLE(prepareTime);

    QSystrace::begin("graphics", "QPA::set", "");
    HWC_PLUGIN_EXPECT_ZERO(hwc_device->set(hwc_device, num_displays, display_list));
    QSystrace::end("graphics", "QPA::set", "");

    QPA_HWC_TIMING_SAMPLE(setTime);

    return content->release();
}

void HwComposerBackend_v11::blankDisplay(int display, bool blank)
{
    int status;
#ifdef HWC_DEVICE_API_VERSION_1_4
    if (hwc_version == HWC_DEVICE_API_VERSION_1_4) {
        status = hwc_device->setPowerMode(hwc_device, display, blank ? HWC_POWER_MODE_OFF : HWC_POWER_MODE_NORMAL);
    } else
#endif
#ifdef HWC_DEVICE_API_VERSION_1_5
    if (hwc_version == HWC_DEVICE_API_VERSION_1_5) {
        status = hwc_device->setPowerMode(hwc_device, display, blank ? HWC_POWER_MODE_OFF : HWC_POWER_MODE_NORMAL);
    } else
#endif
        status = hwc_device->blank(hwc_device, display, blank);
    HWC_PLUGIN_EXPECT_ZERO(status);
    g_unblanked_displays[display] = (status == blank);
}

void
HwComposerBackend_v11::sleepDisplay(bool sleep)
{
    // XXX: For debugging only
    //dump_attributes(hwc_device, num_displays);

    m_displayOff = sleep;
    if (sleep) {
        // Stop the timer so we don't end up calling into eventControl after the
        // screen has been turned off. Doing so leads to logcat errors being
        // logged.
        m_vsyncTimeout.stop();
        hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 0);

        for (int i=0; i<num_displays; i++) {
            if (g_unblanked_displays[i]) {
                blankDisplay(i, true);
            }
        }
    } else {
#if 0
        for (int i=0; i<num_displays; i++) {
            blankDisplay(i, false);
        }
#endif
        if (g_external_connected) {
            if (g_unblanked_displays[0]) {
                fprintf(stderr, "Blanking internal display\n");
                blankDisplay(0, true);
            }
            fprintf(stderr, "Unblanking external display\n");
            blankDisplay(1, false);
        } else {
            if (g_unblanked_displays[1]) {
                fprintf(stderr, "Blanking external display\n");
                blankDisplay(1, true);
            }
            fprintf(stderr, "Unblanking internal display\n");
            blankDisplay(0, false);
        }


        if (hwc_list) {
            hwc_list->flags |= HWC_GEOMETRY_CHANGED;
        }

        // If we have pending updates, make sure those start happening now..
        if (m_pendingUpdate.size()) {
            hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 1);
            m_vsyncTimeout.start(50, this);
        }
    }
}

int HwComposerBackend_v11::getSingleAttribute(uint32_t attribute)
{
    uint32_t config;

    if (hwc_version == HWC_DEVICE_API_VERSION_1_1
#ifdef HWC_DEVICE_API_VERSION_1_2
        || hwc_version == HWC_DEVICE_API_VERSION_1_2
#endif
#ifdef HWC_DEVICE_API_VERSION_1_3
        || hwc_version == HWC_DEVICE_API_VERSION_1_3
#endif
)
    {
        /* 1.3 or lower, currently active config is the first config */
        size_t numConfigs = 1;
        hwc_device->getDisplayConfigs(hwc_device, 0, &config, &numConfigs);
    }
#ifdef HWC_DEVICE_API_VERSION_1_4
    else {
        /* 1.4 or higher */
        if (!qgetenv("QPA_HWC_WORKAROUNDS").split(',').contains("no-active-config")) {
            config = hwc_device->getActiveConfig(hwc_device, 0);
        } else {
            size_t numConfigs = 1;
            hwc_device->getDisplayConfigs(hwc_device, 0, &config, &numConfigs);
        }
    }
#endif

    const uint32_t attributes[] = {
        attribute,
        HWC_DISPLAY_NO_ATTRIBUTE,
    };

    int32_t values[] = {
        0,
        0,
    };

    hwc_device->getDisplayAttributes(hwc_device, 0, config, attributes, values);

    for (unsigned int i = 0; i < sizeof(attributes) / sizeof(uint32_t); i++) {
        if (attributes[i] == attribute) {
            return values[i];
        }
    }

    return 0;
}

float
HwComposerBackend_v11::refreshRate()
{
    float value = (float)getSingleAttribute(HWC_DISPLAY_VSYNC_PERIOD);

    value = (1000000000.0 / value);

    // make sure the value is "reasonable", otherwise fallback to 60.0.
    return (value > 0 && value <= 1000) ? value : 60.0;
}

bool
HwComposerBackend_v11::getScreenSizes(int *width, int *height, float *physical_width, float *physical_height)
{
    int dpi_x = getSingleAttribute(HWC_DISPLAY_DPI_X) / 1000;
    int dpi_y = getSingleAttribute(HWC_DISPLAY_DPI_Y) / 1000;

    *width = getSingleAttribute(HWC_DISPLAY_WIDTH);
    *height = getSingleAttribute(HWC_DISPLAY_HEIGHT);

    if (dpi_x == 0 || dpi_y == 0 || *width == 0 || *height == 0) {
        qWarning() << "failed to read screen size from hwc1.x backend";
        return false;
    }

    *physical_width = (((float)*width) * 25.4) / (float)dpi_x;
    *physical_height = (((float)*height) * 25.4) / (float)dpi_y;

    return true;
}

void HwComposerBackend_v11::timerEvent(QTimerEvent *e)
{
    if (e->timerId() == m_vsyncTimeout.timerId()) {
        hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 0);
        m_vsyncTimeout.stop();
        // When waking up, we might get here as a result of requesting vsync events
        // before the hwc is up and running. If we're timing out while still waiting
        // for vsync to occur, trigger the update so we don't block the UI.
        if (!m_pendingUpdate.isEmpty())
            handleVSyncEvent();
    } else if (e->timerId() == m_deliverUpdateTimeout.timerId()) {
        m_deliverUpdateTimeout.stop();
        handleVSyncEvent();
    }
}

bool HwComposerBackend_v11::event(QEvent *e)
{
    if (e->type() == QEvent::User) {
        static int idleTime = qBound(5, qgetenv("QPA_HWC_IDLE_TIME").toInt(), 100);
        if (!m_deliverUpdateTimeout.isActive())
            m_deliverUpdateTimeout.start(idleTime, this);
        return true;
    }
    return QObject::event(e);
}

void HwComposerBackend_v11::handleVSyncEvent()
{
    QSystraceEvent trace("graphics", "QPA::handleVsync");
    QSet<QWindow *> pendingWindows = m_pendingUpdate;
    m_pendingUpdate.clear();
    foreach (QWindow *w, pendingWindows) {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 12, 0))
        QPlatformWindow *platformWindow = w->handle();
        if (!platformWindow)
            continue;

        platformWindow->deliverUpdateRequest();
#else
        QWindowPrivate *wp = (QWindowPrivate *) QWindowPrivate::get(w);
        wp->deliverUpdateRequest();
#endif
    }
}

bool HwComposerBackend_v11::requestUpdate(QEglFSWindow *window)
{
    // If the display is off, do updates via the normal Qt-based timer.
    if (m_displayOff)
        return false;

    if (m_vsyncTimeout.isActive()) {
        m_vsyncTimeout.stop();
    } else {
        hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 1);
    }
    m_vsyncTimeout.start(50, this);
    m_pendingUpdate.insert(window->window());
    return true;
}

#endif /* HWC_PLUGIN_HAVE_HWCOMPOSER1_API */
