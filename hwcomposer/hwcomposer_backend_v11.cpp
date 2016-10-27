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

#ifdef HWC_PLUGIN_HAVE_HWCOMPOSER1_API

// #define QPA_HWC_TIMING
// #define QPA_HWC_SYNC_BEFORE_SET

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

struct HwcProcs_v11 : public hwc_procs
{
    HwComposerBackend_v11 *backend;
};

static void hwc11_callback_vsync(const struct hwc_procs *procs, int, int64_t)
{
    QCoreApplication::postEvent(static_cast<const HwcProcs_v11 *>(procs)->backend, new QEvent(QEvent::User));
}

static void hwc11_callback_invalidate(const struct hwc_procs *)
{
}

static void hwc11_callback_hotplug(const struct hwc_procs *, int, int)
{
}


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
    int bufferCount = qBound(2, qgetenv("QPA_HWC_BUFFER_COUNT").toInt(), 8);
    setBufferCount(bufferCount);
}

void HWComposer::present(HWComposerNativeWindowBuffer *buffer)
{
    QPA_HWC_TIMING_SAMPLE(presentTime);

    fblayer->handle = buffer->handle;
    fblayer->releaseFenceFd = -1;

#ifdef QPA_HWC_SYNC_BEFORE_SET
    int acqFd = getFenceBufferFd(buffer);
    if (acqFd >= 0) {
        sync_wait(acqFd, -1);
        close(acqFd);
        fblayer->acquireFenceFd = -1;
    }
#else
    fblayer->acquireFenceFd = getFenceBufferFd(buffer);
#endif

    QPA_HWC_TIMING_SAMPLE(syncTime);

    int err = hwcdevice->prepare(hwcdevice, num_displays, mlist);
    HWC_PLUGIN_EXPECT_ZERO(err);

    QPA_HWC_TIMING_SAMPLE(prepareTime);

    err = hwcdevice->set(hwcdevice, num_displays, mlist);
    HWC_PLUGIN_EXPECT_ZERO(err);

    QPA_HWC_TIMING_SAMPLE(setTime);

    setFenceBufferFd(buffer, fblayer->releaseFenceFd);

    if (mlist[0]->retireFenceFd != -1) {
        close(mlist[0]->retireFenceFd);
        mlist[0]->retireFenceFd = -1;
    }
}

HwComposerBackend_v11::HwComposerBackend_v11(hw_module_t *hwc_module, hw_device_t *hw_device, int num_displays)
    : HwComposerBackend(hwc_module)
    , hwc_device((hwc_composer_device_1_t *)hw_device)
    , hwc_list(NULL)
    , hwc_mList(NULL)
    , num_displays(num_displays)
    , m_displayOff(true)
{
    procs = new HwcProcs_v11();
    procs->invalidate = hwc11_callback_invalidate;
    procs->hotplug = hwc11_callback_hotplug;
    procs->vsync = hwc11_callback_vsync;
    procs->backend = this;

    hwc_device->registerProcs(hwc_device, procs);

    hwc_version = interpreted_version(hw_device);
    sleepDisplay(false);
}

HwComposerBackend_v11::~HwComposerBackend_v11()
{
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

    size_t neededsize = sizeof(hwc_display_contents_1_t) + 2 * sizeof(hwc_layer_1_t);
    hwc_list = (hwc_display_contents_1_t *) malloc(neededsize);
    hwc_mList = (hwc_display_contents_1_t **) malloc(num_displays * sizeof(hwc_display_contents_1_t *));
    const hwc_rect_t r = { 0, 0, width, height };

    for (int i = 0; i < num_displays; i++) {
         hwc_mList[i] = NULL;
    }
    // Assign buffer only to the first item, otherwise you get tearing
    // if passed the same to multiple places
    hwc_mList[0] = hwc_list;

    hwc_layer_1_t *layer = NULL;

    layer = &hwc_list->hwLayers[0];
    memset(layer, 0, sizeof(hwc_layer_1_t));
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
    layer->sourceCrop = r;
#endif
    layer->displayFrame = r;
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

    layer = &hwc_list->hwLayers[1];
    memset(layer, 0, sizeof(hwc_layer_1_t));
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
    layer->sourceCrop = r;
#endif
    layer->displayFrame = r;
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

    hwc_list->retireFenceFd = -1;
    hwc_list->flags = HWC_GEOMETRY_CHANGED;
    hwc_list->numHwLayers = 2;
#ifdef HWC_DEVICE_API_VERSION_1_3
    hwc_list->outbuf = 0;
    hwc_list->outbufAcquireFenceFd = -1;
#endif


    HWComposer *hwc_win = new HWComposer(width, height, HAL_PIXEL_FORMAT_RGBA_8888,
                                         hwc_device, hwc_mList, &hwc_list->hwLayers[1], num_displays);
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

void
HwComposerBackend_v11::sleepDisplay(bool sleep)
{
    // TODO: remove debug spamming
    qWarning("m_displayOff: %d -> %d", m_displayOff, sleep);

    if (m_displayOff != sleep) {
        m_displayOff = sleep;

#ifdef HWC_DEVICE_API_VERSION_1_4
        if (hwc_version == HWC_DEVICE_API_VERSION_1_4) {
            HWC_PLUGIN_EXPECT_ZERO(hwc_device->setPowerMode(hwc_device, 0, HWC_POWER_MODE_OFF));
        } else
#endif
#ifdef HWC_DEVICE_API_VERSION_1_5
        if (hwc_version == HWC_DEVICE_API_VERSION_1_5) {
            HWC_PLUGIN_EXPECT_ZERO(hwc_device->setPowerMode(hwc_device, 0, HWC_POWER_MODE_OFF));
        } else
#endif
            HWC_PLUGIN_EXPECT_ZERO(hwc_device->blank(hwc_device, 0, 1));
    } else {
#ifdef HWC_DEVICE_API_VERSION_1_4
        if (hwc_version == HWC_DEVICE_API_VERSION_1_4) {
            HWC_PLUGIN_EXPECT_ZERO(hwc_device->setPowerMode(hwc_device, 0, HWC_POWER_MODE_NORMAL));
        } else
#endif
#ifdef HWC_DEVICE_API_VERSION_1_5
        if (hwc_version == HWC_DEVICE_API_VERSION_1_5) {
            HWC_PLUGIN_EXPECT_ZERO(hwc_device->setPowerMode(hwc_device, 0, HWC_POWER_MODE_NORMAL));
        } else
#endif
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

void HwComposerBackend_v11::timerEvent(QTimerEvent *e)
{
    if (e->timerId() == m_vsyncTimeout.timerId()) {
        if (!m_displayOff) {
            hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 0);
        }
        else {
            // TODO: remove debug spamming
            qWarning("skip hwc_device->eventControl(...)");
        }
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
        static int idleTime = qBound(0, qgetenv("QPA_HWC_IDLE_TIME").toInt(), 100);
        m_deliverUpdateTimeout.start(idleTime, this);
        return true;
    }
    return QObject::event(e);
}

void HwComposerBackend_v11::handleVSyncEvent()
{
    QSet<QWindow *> pendingWindows = m_pendingUpdate;
    m_pendingUpdate.clear();
    foreach (QWindow *w, pendingWindows) {
        QWindowPrivate *wp = (QWindowPrivate *) QWindowPrivate::get(w);
        wp->deliverUpdateRequest();
    }
}

bool HwComposerBackend_v11::requestUpdate(QEglFSWindow *window)
{
    // If the display is off, do updates via the normal Qt-based timer.
    if (m_vsyncTimeout.isActive()) {
        m_vsyncTimeout.stop();
    } else {
        if (!m_displayOff)
            hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 1);
        else {
            // TODO: remove debug spamming
            qWarning("skip hwc_device->eventControl(...)");
        }
    }
    m_vsyncTimeout.start(50, this);
    m_pendingUpdate.insert(window->window());
    return true;
}

#endif /* HWC_PLUGIN_HAVE_HWCOMPOSER1_API */
