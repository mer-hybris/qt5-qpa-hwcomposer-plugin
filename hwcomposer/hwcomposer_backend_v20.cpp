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
#include "hwcomposer_backend_v20.h"
#include "qeglfswindow.h"

#include <string>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimerEvent>
#include <QtCore/QCoreApplication>
#include <private/qwindow_p.h>

#include "qsystrace_selector.h"

#include <inttypes.h>

// #ifdef HWC_PLUGIN_HAVE_HWCOMPOSER1_API

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

struct HwcProcs_v20 : public HWC2EventListener
{
    HwComposerBackend_v20 *backend;
    hwc2_display_t primaryDisplayId;
};

void hwc2_callback_vsync(HWC2EventListener* listener, int32_t /*sequenceId*/,
                         hwc2_display_t /*display*/, int64_t /*timestamp*/)
{
    static int counter = 0;
    ++counter;
    if (counter % 2)
        QSystrace::begin("graphics", "QPA::vsync", "");
    else
        QSystrace::end("graphics", "QPA::vsync", "");

    QCoreApplication::postEvent(static_cast<const HwcProcs_v20 *>(listener)->backend,
                                new QEvent(QEvent::User));
}

void hwc2_callback_hotplug(HWC2EventListener* listener, int32_t sequenceId,
                           hwc2_display_t display, bool connected,
                           bool primaryDisplay)
{
    qDebug("onHotplugReceived(%d, %" PRIu64 ", %s, %s)",
           sequenceId, display,
           connected ? "connected" : "disconnected",
           primaryDisplay ? "primary" : "external");

    if (primaryDisplay) {
        static_cast<HwcProcs_v20 *>(listener)->primaryDisplayId = display;
    }

    static_cast<const HwcProcs_v20 *>(listener)->backend->onHotplugReceived(
        sequenceId, display, connected, primaryDisplay);
}

void hwc2_callback_refresh(HWC2EventListener* /*listener*/, int32_t /*sequenceId*/,
                           hwc2_display_t /*display*/)
{
}

class HWC2Window : public HWComposerNativeWindow
{
    private:
        hwc2_compat_layer_t *layer;
        hwc2_compat_display_t *hwcDisplay;
        int lastPresentFence = -1;
        bool m_syncBeforeSet;
    protected:
        void present(HWComposerNativeWindowBuffer *buffer);

    public:

        HWC2Window(unsigned int width, unsigned int height, unsigned int format,
                hwc2_compat_display_t *display, hwc2_compat_layer_t *layer);
        ~HWC2Window();
        void set();
};

HWC2Window::HWC2Window(unsigned int width, unsigned int height,
                    unsigned int format, hwc2_compat_display_t* display,
                    hwc2_compat_layer_t *layer) :
                    HWComposerNativeWindow(width, height, format),
                    layer(layer), hwcDisplay(display)
{
    int bufferCount = qgetenv("QPA_HWC_BUFFER_COUNT").toInt();
    if (bufferCount)
        bufferCount = qBound(2, bufferCount, 8);
    else
        // default to triple-buffering as on Android
        bufferCount = 3;
    setBufferCount(bufferCount);
    m_syncBeforeSet = qEnvironmentVariableIsSet("QPA_HWC_SYNC_BEFORE_SET");
}

HWC2Window::~HWC2Window()
{
    if (lastPresentFence != -1) {
        close(lastPresentFence);
    }
}

void HWC2Window::present(HWComposerNativeWindowBuffer *buffer)
{
    uint32_t numTypes = 0;
    uint32_t numRequests = 0;
    int displayId = 0;
    hwc2_error_t error = HWC2_ERROR_NONE;

    QSystraceEvent trace("graphics", "QPA::present");

    QPA_HWC_TIMING_SAMPLE(presentTime);

    int acquireFenceFd = getFenceBufferFd(buffer);

    if (m_syncBeforeSet && acquireFenceFd >= 0) {
        sync_wait(acquireFenceFd, -1);
        close(acquireFenceFd);
        acquireFenceFd = -1;
    }

    error = hwc2_compat_display_validate(hwcDisplay, &numTypes,
                                                    &numRequests);
    if (error != HWC2_ERROR_NONE && error != HWC2_ERROR_HAS_CHANGES) {
        qDebug("prepare: validate failed for display %d: %d", displayId, error);
        return;
    }

    if (numTypes || numRequests) {
        qDebug("prepare: validate required changes for display %d: %d",
               displayId, error);
        return;
    }

    error = hwc2_compat_display_accept_changes(hwcDisplay);
    if (error != HWC2_ERROR_NONE) {
        qDebug("prepare: acceptChanges failed: %d", error);
        return;
    }

    QPA_HWC_TIMING_SAMPLE(prepareTime);

    QSystrace::begin("graphics", "QPA::set_client_target", "");
    hwc2_compat_display_set_client_target(hwcDisplay, /* slot */0, buffer,
                                          acquireFenceFd,
                                          HAL_DATASPACE_UNKNOWN);
    QSystrace::end("graphics", "QPA::set_client_target", "");

    QSystrace::begin("graphics", "QPA::present", "");
    int presentFence = -1;
    hwc2_compat_display_present(hwcDisplay, &presentFence);
    QSystrace::end("graphics", "QPA::present", "");

    QPA_HWC_TIMING_SAMPLE(setTime);

    if (lastPresentFence != -1) {
        sync_wait(lastPresentFence, -1);
        close(lastPresentFence);
    }

    lastPresentFence = presentFence != -1 ? dup(presentFence) : -1;

    setFenceBufferFd(buffer, presentFence);
}

int HwComposerBackend_v20::composerSequenceId = 0;

HwComposerBackend_v20::HwComposerBackend_v20(hw_module_t *hwc_module, void *libminisf)
    : HwComposerBackend(hwc_module, libminisf)
    , hwc2_device(NULL)
    , hwc2_primary_display(NULL)
    , hwc2_primary_layer(NULL)
    , m_displayOff(true)
{
    procs = new HwcProcs_v20();
    procs->on_vsync_received = hwc2_callback_vsync;
    procs->on_hotplug_received = hwc2_callback_hotplug;
    procs->on_refresh_received = hwc2_callback_refresh;
    procs->backend = this;
    // primaryDisplayId is not changed in this constructor
    // but it may be changed by hwc2_callback_hotplug and thus the
    // hwc2_compat_device_get_display_by_id loop will request the
    // correct primary diplay once primaryDisplayId has been set.
    procs->primaryDisplayId = 0;

    hwc2_device = hwc2_compat_device_new(false);
    HWC_PLUGIN_ASSERT_NOT_NULL(hwc2_device);

    hwc2_compat_device_register_callback(hwc2_device, procs,
        HwComposerBackend_v20::composerSequenceId++);

    for (int i = 0; i < 5 * 1000; ++i) {
        // Wait at most 5s for hotplug events
        if ((hwc2_primary_display =
            hwc2_compat_device_get_display_by_id(hwc2_device, procs->primaryDisplayId)))
            break;
        usleep(1000);
    }
    HWC_PLUGIN_ASSERT_NOT_NULL(hwc2_primary_display);

    sleepDisplay(false);
}

HwComposerBackend_v20::~HwComposerBackend_v20()
{
    hwc2_compat_display_set_vsync_enabled(hwc2_primary_display, HWC2_VSYNC_DISABLE);

    hwc2_compat_display_set_power_mode(hwc2_primary_display, HWC2_POWER_MODE_OFF);

    // Close the hwcomposer handle
    if (!qgetenv("QPA_HWC_WORKAROUNDS").split(',').contains("no-close-hwc"))
        free(hwc2_device);

    if (hwc2_primary_display != NULL) {
        free(hwc2_primary_display);
    }

    delete procs;
}

EGLNativeDisplayType
HwComposerBackend_v20::display()
{
    return EGL_DEFAULT_DISPLAY;
}

EGLNativeWindowType
HwComposerBackend_v20::createWindow(int width, int height)
{
    // We expect that we haven't created a window already, if we had, we
    // would leak stuff, and we want to avoid that for obvious reasons.
    HWC_PLUGIN_EXPECT_NULL(hwc2_primary_layer);

    hwc2_compat_layer_t* layer = hwc2_primary_layer =
        hwc2_compat_display_create_layer(hwc2_primary_display);

    hwc2_compat_layer_set_composition_type(layer, HWC2_COMPOSITION_CLIENT);
    hwc2_compat_layer_set_blend_mode(layer, HWC2_BLEND_MODE_NONE);
    hwc2_compat_layer_set_source_crop(layer, 0.0f, 0.0f, width, height);
    hwc2_compat_layer_set_display_frame(layer, 0, 0, width, height);
    hwc2_compat_layer_set_visible_region(layer, 0, 0, width, height);

    HWC2Window *hwc_win = new HWC2Window(width, height,
                                         HAL_PIXEL_FORMAT_RGBA_8888,
                                         hwc2_primary_display, layer);

    return (EGLNativeWindowType) static_cast<ANativeWindow *>(hwc_win);
}

void
HwComposerBackend_v20::destroyWindow(EGLNativeWindowType window)
{
    Q_UNUSED(window);
}

void
HwComposerBackend_v20::swap(EGLNativeDisplayType display, EGLSurface surface)
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
HwComposerBackend_v20::sleepDisplay(bool sleep)
{
    m_displayOff = sleep;
    if (sleep) {
        // Stop the timer so we don't end up calling into eventControl after the
        // screen has been turned off. Doing so leads to logcat errors being
        // logged.
        m_vsyncTimeout.stop();
        hwc2_compat_display_set_vsync_enabled(hwc2_primary_display, HWC2_VSYNC_DISABLE);

        hwc2_compat_display_set_power_mode(hwc2_primary_display, HWC2_POWER_MODE_OFF);
    } else {
        hwc2_compat_display_set_power_mode(hwc2_primary_display, HWC2_POWER_MODE_ON);

        // If we have pending updates, make sure those start happening now..
        if (m_pendingUpdate.size()) {
            hwc2_compat_display_set_vsync_enabled(hwc2_primary_display, HWC2_VSYNC_ENABLE);
            m_vsyncTimeout.start(50, this);
        }
    }
}

float
HwComposerBackend_v20::refreshRate()
{
    float value = (float)hwc2_compat_display_get_active_config(hwc2_primary_display)->vsyncPeriod;

    value = (1000000000.0 / value);

    // make sure the value is "reasonable", otherwise fallback to 60.0.
    return (value > 0 && value <= 1000.0) ? value : 60.0;
}

bool
HwComposerBackend_v20::getScreenSizes(int *width, int *height, float *physical_width, float *physical_height)
{
    HWC2DisplayConfig *config = hwc2_compat_display_get_active_config(hwc2_primary_display);

    // should not happen
    if (!config) return false;

    int dpi_x = config->dpiX;
    int dpi_y = config->dpiY;

    *width = config->width;
    *height = config->height;

    if (dpi_x == 0 || dpi_y == 0 || *width == 0 || *height == 0) {
        qWarning() << "failed to read screen size from hwc1.x backend";
        return false;
    }

    *physical_width = (((float)*width) * 25.4) / (float)dpi_x;
    *physical_height = (((float)*height) * 25.4) / (float)dpi_y;

    return true;
}

void HwComposerBackend_v20::timerEvent(QTimerEvent *e)
{
    if (e->timerId() == m_vsyncTimeout.timerId()) {
        hwc2_compat_display_set_vsync_enabled(hwc2_primary_display, HWC2_VSYNC_DISABLE);
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

bool HwComposerBackend_v20::event(QEvent *e)
{
    if (e->type() == QEvent::User) {
        static int idleTime = qBound(5, qgetenv("QPA_HWC_IDLE_TIME").toInt(), 100);
        if (!m_deliverUpdateTimeout.isActive())
            m_deliverUpdateTimeout.start(idleTime, this);
        return true;
    }
    return QObject::event(e);
}

void HwComposerBackend_v20::handleVSyncEvent()
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

bool HwComposerBackend_v20::requestUpdate(QEglFSWindow *window)
{
    // If the display is off, do updates via the normal Qt-based timer.
    if (m_displayOff)
        return false;

    if (m_vsyncTimeout.isActive()) {
        m_vsyncTimeout.stop();
    } else {
        hwc2_compat_display_set_vsync_enabled(hwc2_primary_display, HWC2_VSYNC_ENABLE);
    }
    m_vsyncTimeout.start(50, this);
    m_pendingUpdate.insert(window->window());
    return true;
}

void HwComposerBackend_v20::onHotplugReceived(int32_t /*sequenceId*/,
                                        hwc2_display_t display, bool connected,
                                        bool /*primaryDisplay*/)
{
    hwc2_compat_device_on_hotplug(hwc2_device, display, connected);
}

// #endif /* HWC_PLUGIN_HAVE_HWCOMPOSER1_API */
