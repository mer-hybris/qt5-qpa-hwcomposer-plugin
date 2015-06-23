/****************************************************************************
**
** Copyright (C) 2013, 2015 Jolla Ltd.
** Contact: Thomas Perl <thomas.perl@jolla.com>
** Contact: Gunnar Sletta <gunnar.sletta@jollamobile.com>
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

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QEvent>
#include <QCoreApplication>
#include <QSize>
#include <QRect>

#include <private/qwindow_p.h>
#include <private/qsystrace_p.h>

#ifdef HWC_PLUGIN_HAVE_HWCOMPOSER1_API

class HWC11Thread;

class HWC11WindowSurface : public HWComposerNativeWindow
{
protected:
    void present(HWComposerNativeWindowBuffer *buffer) { backend->present(buffer); }
    // int dequeueBuffer(BaseNativeWindowBuffer** buffer, int* fenceFd) {
    //     int result = HWComposerNativeWindow::dequeueBuffer(buffer, fenceFd);
    //     qDebug() << " --- dequeueBuffer" << *buffer << *fenceFd << (*buffer)->handle << QThread::currentThread();
    //     return result;
    // }



public:
    unsigned int width() const { return HWComposerNativeWindow::width(); }
    unsigned int height() const { return HWComposerNativeWindow::height(); }
    HWC11WindowSurface(HwComposerBackend_v11 *backend, unsigned int width, unsigned int height, unsigned int format);

private:
    HwComposerBackend_v11 *backend;
};

static QEvent::Type HWC11_VSYNC_EVENT = (QEvent::Type)(QEvent::User+1);
static QEvent::Type HWC11_INVALIDATE_EVENT = (QEvent::Type)(QEvent::User+2);

class HWC11Thread : public QThread, public hwc_procs
{
public:
    enum Action {
        InitializeAction = QEvent::User,
        CleanupAction,
        DisplaySleepAction,
        DisplayWakeAction,
        CheckLayerListAction,
        ResetLayerListAction,
        EglSurfaceCompositionAction,
        LayerListCompositionAction,
        ActivateVSyncCallbackAction,
        DeactivateVSyncCallbackAction
    };

    HWC11Thread(HwComposerBackend_v11 *backend, hwc_composer_device_1_t *d);

    void composeEglSurface();
    void composeAcceptedLayerList();
    void doComposition(hwc_display_contents_1_t *dc);
    void initialize();
    void cleanup();
    void checkLayerList();
    void syncAndCloseOldFences();

    void post(Action a) { QCoreApplication::postEvent(this, new QEvent((QEvent::Type) a)); }
    bool event(QEvent *e);

    inline void lock() { mutex.lock(); }
    inline void unlock() { mutex.unlock(); }
    inline void wait() { condition.wait(&mutex); }
    inline void wake() { condition.wakeOne(); }

    void stopGracefully() {
        post(CleanupAction);
        lock();
        size = QSize();
        unlock();
        quit();
    }

    HwComposerBackend_v11 *backend;
    hwc_composer_device_1_t *hwcDevice;
    hwc_display_contents_1_t *hwcEglSurfaceList;
    hwc_display_contents_1_t *hwcLayerList;
    buffer_handle_t lastEglSurfaceBuffer;

    struct BufferAndFd {
        buffer_handle_t buffer;
        int fd;
    };
    QVarLengthArray<BufferAndFd, 8> m_releaseFences;

    QSize size;

    QMutex layerListMutex;
    HwcInterface::LayerList *acceptedLayerList;

    // The following values is the state of the upcoming composition.
    HWComposerNativeWindowBuffer *eglSurfaceBuffer;
    bool useLayerList;

    // Mutex/wait condition to be used when updating the upcoming composition state
    QMutex mutex;
    QWaitCondition condition;
};

void hwc11_copy_layer_list(QVarLengthArray<void *, 8> *dst, HwcInterface::LayerList *src)
{
    dst->resize(src->layerCount);
    for (int i=0; i<src->layerCount; ++i)
        (*dst)[i] = src->layers[i].handle;
}


HWC11WindowSurface::HWC11WindowSurface(HwComposerBackend_v11 *b, unsigned int width, unsigned int height, unsigned int format)
    : HWComposerNativeWindow(width, height, format)
    , backend(b)
{
}


HwComposerBackend_v11::HwComposerBackend_v11(hw_module_t *hwc_module, hw_device_t *hw_device, int num_displays)
    : HwComposerBackend(hwc_module)
    , m_scheduledLayerList(0)
    , m_releaseLayerListCallback(0)
    , m_bufferAvailableCallback(0)
    , m_bufferAvailableCallbackData(0)
    , m_invalidateCallback(0)
    , m_invalidateCallbackData(0)
    , m_eglSurfaceBuffer(0)
    , m_eglWithLayerList(false)
    , m_vsyncCountDown(0)
    , m_timeToUpdateTimer(0)
    , m_swappingLayersOnly(0)

{
    Q_UNUSED(num_displays);
    m_thread = new HWC11Thread(this, (hwc_composer_device_1_t *) hw_device);
    m_thread->moveToThread(m_thread);
    m_thread->post(HWC11Thread::InitializeAction);
}

HwComposerBackend_v11::~HwComposerBackend_v11()
{
    // Stop the compositor thread

    if (m_thread->isRunning())
        m_thread->stopGracefully();
    m_thread->QThread::wait();
    delete m_thread;
}

EGLNativeDisplayType
HwComposerBackend_v11::display()
{
    return EGL_DEFAULT_DISPLAY;
}

EGLNativeWindowType
HwComposerBackend_v11::createWindow(int width, int height)
{
    qCDebug(QPA_LOG_HWC, "createWindow: %d x %d", width, height);
    // We only support a single window
    HWC11WindowSurface *window = new HWC11WindowSurface(this, width, height, HAL_PIXEL_FORMAT_RGBA_8888);
    Q_ASSERT(!m_thread->isRunning());
    m_thread->size = QSize(width, height);
    m_thread->start();
    return (EGLNativeWindowType) static_cast<ANativeWindow *>(window);
}

void
HwComposerBackend_v11::destroyWindow(EGLNativeWindowType window)
{
    Q_UNUSED(window);
    qCDebug(QPA_LOG_HWC, "destroyWindow");
    Q_ASSERT(m_thread->isRunning());
    // Stop rendering...
    m_thread->stopGracefully();

    // No need to delete the window, refcounting in libhybris will handle that
    // as a result of this call stemming from where the platfrom plugin calls
    // eglDestroyWindowSurface.
}

/* Sets the buffer as the current front buffer to be displayed through the
   HWC. The HWC will pick up the buffer and set it to 0.

   If there already is a buffer pending for display, this function will block
   until the current buffer has been picked up. As HwcWindowSurfaceNativeWindow
   has two buffers by default, this allows us to queue up one buffer before
   rendering is blocked on the EGL render thread.
 */
void HwComposerBackend_v11::present(HWComposerNativeWindowBuffer *b)
{
    QSystraceEvent systrace("graphics", "QPA/HWC::present");
    qCDebug(QPA_LOG_HWC, "present: %p (%p), current=%p, layerList=%d, thread=%p", b, b->handle, m_eglSurfaceBuffer, m_layerListBuffers.size(), QThread::currentThread());
    m_thread->lock();
    if (waitForComposer()) {
        qCDebug(QPA_LOG_HWC, " - need to wait for composer... %p", QThread::currentThread());
        m_thread->wait();
    }
    Q_ASSERT(m_eglSurfaceBuffer == 0);
    Q_ASSERT(m_layerListBuffers.size() == 0);
    m_eglSurfaceBuffer = b;
    if (m_eglWithLayerList) {
        // present is called directly from eglSwapBuffers, so the acceptedLayerList will be
        // the same as the input to swapLayerList, so we pick the buffer values from here.
        hwc11_copy_layer_list(&m_layerListBuffers, m_thread->acceptedLayerList);
        m_thread->post(HWC11Thread::LayerListCompositionAction);
    } else {
        m_thread->post(HWC11Thread::EglSurfaceCompositionAction);
    }
    m_thread->unlock();
}

void HwComposerBackend_v11::deliverUpdateRequests()
{
    QSystraceEvent systrace("graphics", "QPA/HWC::deliverUpdateRequest");
    qCDebug(QPA_LOG_HWC, " - delivering update request, %d windows pending", m_windowsPendingUpdate.size());
    QSet<QWindow *> toUpdate = m_windowsPendingUpdate;
    m_windowsPendingUpdate.clear();
    foreach (QWindow *w, toUpdate) {
        QWindowPrivate *wd = (QWindowPrivate *) QWindowPrivate::get(w);
        wd->deliverUpdateRequest();
    }
}

void HwComposerBackend_v11::timerEvent(QTimerEvent *te)
{
    if (te->timerId() == m_timeToUpdateTimer) {
        deliverUpdateRequests();
        killTimer(m_timeToUpdateTimer);
        m_timeToUpdateTimer = 0;
    }
}

void HwComposerBackend_v11::startVSyncCountdown()
{
    if (m_vsyncCountDown <= 0) {
        m_thread->post(HWC11Thread::ActivateVSyncCallbackAction);
    }
    m_vsyncCountDown = 60; // keep spinning for one second...
}

void HwComposerBackend_v11::stopVSyncCountdown()
{
    m_thread->post(HWC11Thread::DeactivateVSyncCallbackAction);
    m_vsyncCountDown = 0;
}


/*
    Forwarded from QPlatformWindow::requestUpdate() for the fullscreen window.

    There are two ways to schedule updates. When combine HWC with EGL
    rendering,  either by using only EGL or when using EGL + one or more
    layers, we rely on the default mechanism. This will schedule a 5ms timer
    (in QPlatformWindow::requestUpdate()) and trigger the renderloop in
    declarative to go into the polishAndSync phase. The overall effect of this
    is that the GUI thread starts as soon as possible and the render thread
    starts as soon as possible resulting in that the GPU has as much time as
    possible to complete a frame in time for the next vsync. When swapping
    with EGL content, swap() and swapLayerList() will block only if there
    already is a buffer pending, meaning that we block on a full buffer queue,
    meaning that we render ahead of time. A side effect of this mode is that
    layers will be retained in the HWC for at least 2 VSYNCs, which results in
    that clients will have only a partial frame to render its buffer and
    return it to the compositor. In practice this will clamp clients at 30fps,
    which is "the best we can do" if we want the homescreen to stay responsive
    at 60fps. This mode benefits from the fact that many clients are static so
    the limitation is not always visible in practice.

    The second mode of operation is when we are only swapping layers. In this
    mode, we want to prioritize client applications and retain buffers for as
    short a time as possible. We want to take buffers into use close to vsync,
    then do a quick prepare&set and then make sure we release old buffers
    once the composition is complete. It works like this:

    RequestUpdate on Gui thread
        -> start listening for vsync if we're not already
    SwapLayerList on RenderThread
        -> posts lists to HWC thread
        -> waits for composition to complete.
    Composition starts on HWC threaed
    VSync happens
        -> posts an event to GUI thread which in turn calls onVsync
    onVSync() is called on GUI thread
        -> starts a 5ms timer to call deliverUpdateRequest()
    Composition is done on HWC thread
        -> old buffers are released, app notified
    SwapLayerList on RenderThread completes
    Wayland buffers get released on GUI thread.
    Timer to call deliverUpdateRequest triggers
        -> new render pass is initiated.

    In the transition point between rendering modes, we might end up with scheduling
    render either a bit early or a bit late, but all in all, it shouldn't be too bad.

    This whole problem stems from the fact that lipstick is both an OpenGL
    homescreen and a compositor and surfaces are intermixed with homescreen
    content, so we can't do "clean composition" (aka the second mode) like we
    would have liked.
 */
bool HwComposerBackend_v11::requestUpdate(QWindow *window)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    if (m_swappingLayersOnly.load()) {
        QSystraceEvent systrace("graphics", "QPA/HWC::requestUpdate(layers-only)");
        qCDebug(QPA_LOG_HWC) << "Requesting update after next vsync on" << window;
        m_windowsPendingUpdate << window;
        startVSyncCountdown();
        return true;;
    } else {
        QSystraceEvent systrace("graphics", "QPA/HWC::requestUpdate(normal)");
        if (m_vsyncCountDown > 0)
            stopVSyncCountdown();
        qCDebug(QPA_LOG_HWC) << "Requesting update in the near future on" << window;
        return false;
    }
}

bool HwComposerBackend_v11::event(QEvent *e)
{
    if (e->type() == HWC11_VSYNC_EVENT) {
        onVSync();
        return true;
    } else if (e->type() == HWC11_INVALIDATE_EVENT) {
        m_swappingLayersOnly = 0;
        stopVSyncCountdown();
        if (m_invalidateCallback)
            m_invalidateCallback(m_invalidateCallbackData);
        deliverUpdateRequests();
        return true;
    }
    return QObject::event(e);
}

void HwComposerBackend_v11::onVSync()
{
    QSystraceEvent systrace("graphics", "QPA/HWC::onVSync");
    qCDebug(QPA_LOG_HWC, "VSync event delivered to GUI thread");
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    if (!m_timeToUpdateTimer)
        m_timeToUpdateTimer = startTimer(5);
    if (m_vsyncCountDown > 0) {
        --m_vsyncCountDown;
        if (!m_vsyncCountDown)
            stopVSyncCountdown();
    }
}

void
HwComposerBackend_v11::swap(EGLNativeDisplayType display, EGLSurface surface)
{
    QSystraceEvent systrace("graphics", "QPA/HWC::swap");
    qCDebug(QPA_LOG_HWC, "eglSwapBuffers");
    m_eglWithLayerList = false;
    m_swappingLayersOnly = 0;
    eglSwapBuffers(display, surface);
}

void
HwComposerBackend_v11::sleepDisplay(bool sleep)
{
    qCDebug(QPA_LOG_HWC, "sleep: %d", sleep);
    if (sleep) {
        // Stop vsync before shutting of the display to avoid HWC from getting
        // into a bad state
        stopVSyncCountdown();
        m_thread->post(HWC11Thread::DisplaySleepAction);
    } else {
        // Start vsync after starting the display to avoid HWC getting into a
        // bad state...
        m_thread->post(HWC11Thread::DisplayWakeAction);
        startVSyncCountdown();
    }
}

float
HwComposerBackend_v11::refreshRate()
{
    // TODO: Implement new hwc 1.1 querying of vsync period per-display
    //
    // from HwcWindowSurface_defs.h:
    // "This query is not used for HWC_DEVICE_API_VERSION_1_1 and later.
    //  Instead, the per-display attribute HWC_DISPLAY_VSYNC_PERIOD is used."
    return 60.0;
}

void HwComposerBackend_v11::scheduleLayerList(HwcInterface::LayerList *list)
{
    QSystraceEvent systrace("graphics", "QPA/HWC::scheduleLayerList");
    qCDebug(QPA_LOG_HWC, "scheduleLayerList");

    m_swappingLayersOnly = 0;

    if (!m_releaseLayerListCallback)
        qFatal("ReleaseLayerListCallback has not been installed");
    if (!m_bufferAvailableCallback)
        qFatal("BufferAvailableCallback has not been installed");
    if (!m_invalidateCallback)
        qFatal("InvalidateCallback has not been installed");

    m_thread->layerListMutex.lock();

    if (m_scheduledLayerList)
        m_releaseLayerListCallback(m_scheduledLayerList);

    if (!list) {
        m_scheduledLayerList = 0;
        m_thread->post(HWC11Thread::ResetLayerListAction);
    } else {
        for (int i=0; i<list->layerCount; ++i) {
            if (!list->layers[i].handle)
                qFatal("missing buffer handle for layer %d", i);
        }
        m_scheduledLayerList = list;
        m_thread->post(HWC11Thread::CheckLayerListAction);
    }
    m_thread->layerListMutex.unlock();
}

const HwcInterface::LayerList *HwComposerBackend_v11::acceptedLayerList() const
{
    m_thread->layerListMutex.lock();
    HwcInterface::LayerList *list = m_thread->acceptedLayerList;
    m_thread->layerListMutex.unlock();
    return list;
}

void HwComposerBackend_v11::swapLayerList(HwcInterface::LayerList *list)
{
    qCDebug(QPA_LOG_HWC, "swapLayerList, thread=%p", QThread::currentThread());

    m_swappingLayersOnly = int(!list->eglRenderingEnabled);

    if (list != acceptedLayerList())
        qFatal("submitted list is not accepted list");
    if (m_scheduledLayerList)
        qFatal("submitted layerlist while there is a pending 'scheduledLayerList'");

    if (list->eglRenderingEnabled) {
        QSystraceEvent systrace("graphics", "QPA/HWC::swapLayerList(EGL)");
        m_eglWithLayerList = true; // will be picked up in present() which is called from eglSwapBuffers()
        EGLDisplay display = eglGetCurrentDisplay();
        EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
        qCDebug(QPA_LOG_HWC, " - with eglSwapBuffers, display=%p, surface=%p", display, surface);
        eglSwapBuffers(display, surface);

    } else {
        QSystraceEvent systrace("graphics", "QPA/HWC::swapLayerList(HWC)");
        qCDebug(QPA_LOG_HWC, " - swapping layers directly: m_eglSurfaceBuffer=%p, m_layerListBuffers.size()=%d", m_eglSurfaceBuffer, m_layerListBuffers.size());
        m_thread->lock();
        if (waitForComposer()) {
            qCDebug(QPA_LOG_HWC, " - wait for composer");;
            m_thread->wait();
        }

        Q_ASSERT(m_eglSurfaceBuffer == 0);
        Q_ASSERT(m_layerListBuffers.size() == 0);
        hwc11_copy_layer_list(&m_layerListBuffers, list);
        m_thread->post(HWC11Thread::LayerListCompositionAction);

        // When swapping pure layer lists we want to lock down the render
        // thread until composition is done to avoid rendering ahead of time
        // and to line up the swap phase more cleanly with vsync. See the
        // comments in requestUpdate() above.
        if (m_layerListBuffers.size()) {
            qCDebug(QPA_LOG_HWC, " - waiting for swap to complete");
            m_thread->wait();
        }
        qCDebug(QPA_LOG_HWC, " - swapLayerList is all done...");
        m_thread->unlock();
    }
}

static void hwc11_dump_display_contents(hwc_display_contents_1_t *dc)
{
    qCDebug(QPA_LOG_HWC, "                                (HWCT)  - displayContents, retireFence=%d, outbuf=%p, outAcqFence=%d, flags=%x, numLayers=%d",
            dc->retireFenceFd,
            dc->outbuf,
            dc->outbufAcquireFenceFd,
            (int) dc->flags,
            (int) dc->numHwLayers);
    for (unsigned int i=0; i<dc->numHwLayers; ++i) {
        const hwc_layer_1_t &l = dc->hwLayers[i];
        qCDebug(QPA_LOG_HWC, "                                (HWCT)    - layer comp=%x, hints=%x, flags=%x, handle=%p, transform=%x, blending=%x, "
                "src=(%d %d - %dx%d), dst=(%d %d - %dx%d), afd=%d, rfd=%d, a=%d, "
                "region=(%d %d - %dx%d)",
                l.compositionType, l.hints, l.flags, l.handle, l.transform, l.blending,
                (int) l.sourceCropf.left, (int) l.sourceCropf.top, (int) l.sourceCropf.right, (int) l.sourceCropf.bottom,
                l.displayFrame.left, l.displayFrame.top, l.displayFrame.right, l.displayFrame.bottom,
                l.acquireFenceFd, l.releaseFenceFd, l.planeAlpha,
                l.visibleRegionScreen.rects[0].left,
                l.visibleRegionScreen.rects[0].top,
                l.visibleRegionScreen.rects[0].right,
                l.visibleRegionScreen.rects[0].bottom);
    }
}

static void hwc11_callback_vsync(const struct hwc_procs *procs, int, int64_t)
{
    QSystraceEvent systrace("graphics", "QPA/HWC::vsync-callback");
    qCDebug(QPA_LOG_HWC, "callback_vsync");
    const HWC11Thread *thread = static_cast<const HWC11Thread *>(procs);
    QCoreApplication::postEvent(thread->backend, new QEvent(HWC11_VSYNC_EVENT));
}

static void hwc11_callback_invalidate(const struct hwc_procs *procs)
{
    QSystraceEvent systrace("graphics", "QPA/HWC::invalidate-callback");
    qCDebug(QPA_LOG_HWC, "callback_invalidate");
    const HWC11Thread *thread = static_cast<const HWC11Thread *>(procs);
    QCoreApplication::postEvent(thread->backend, new QEvent(HWC11_INVALIDATE_EVENT));
}

static void hwc11_callback_hotplug(const struct hwc_procs *, int, int)
{
    QSystraceEvent systrace("graphics", "QPA/HWC::hotplug-callback");
    qCDebug(QPA_LOG_HWC, "callback_hotplug");
}

HWC11Thread::HWC11Thread(HwComposerBackend_v11 *b, hwc_composer_device_1_t *d)
    : backend(b)
    , hwcDevice(d)
    , hwcEglSurfaceList(0)
    , hwcLayerList(0)
    , lastEglSurfaceBuffer(0)
    , acceptedLayerList(0)
    , eglSurfaceBuffer(0)
    , useLayerList(false)
{
    setObjectName("QPA/HWC Thread");
}

static void hwc11_populate_layer(hwc_layer_1_t *layer, const QRect &tr, const QRect &sr, buffer_handle_t handle, int32_t type)
{
    layer->handle = handle;
    layer->hints = 0;
    layer->flags = 0;
    layer->compositionType = type;
    layer->blending = HWC_BLENDING_PREMULT;
    layer->transform = 0;
    layer->acquireFenceFd = -1;
    layer->releaseFenceFd = -1;
#ifdef HWC_DEVICE_API_VERSION_1_2
    layer->planeAlpha = 0xff;
#endif
#ifdef HWC_DEVICE_API_VERSION_1_3
    layer->sourceCropf.left = sr.x();
    layer->sourceCropf.top = sr.y();
    layer->sourceCropf.right = sr.width() + sr.x();
    layer->sourceCropf.bottom = sr.height() + sr.y();
#else
    layer->sourceCrop.left = sr.x();
    layer->sourceCrop.top = sr.y();
    layer->sourceCrop.right = sr.width() + sr.x();
    layer->sourceCrop.bottom = sr.height() + sr.y();
#endif
    layer->displayFrame.left = tr.x();
    layer->displayFrame.top = tr.y();
    layer->displayFrame.right = tr.width() + tr.x();
    layer->displayFrame.bottom = tr.height() + tr.y();
    layer->visibleRegionScreen.numRects = 1;
    layer->visibleRegionScreen.rects = &layer->displayFrame;
}

static void hwc11_update_layer(hwc_layer_1_t *layer, int acqFd, buffer_handle_t handle)
{
    layer->handle = handle;
    layer->acquireFenceFd = acqFd;
    layer->releaseFenceFd = -1;
    layer->hints = 0;
}

void HWC11Thread::initialize()
{
    qCDebug(QPA_LOG_HWC, "                                (HWCT) initialize");
    Q_ASSERT(size.width() > 1 && size.height() > 1);

    invalidate = hwc11_callback_invalidate;
    hotplug = hwc11_callback_hotplug;
    vsync = hwc11_callback_vsync;
    hwcDevice->registerProcs(hwcDevice, static_cast<hwc_procs *>(this));
    hwcDevice->eventControl(hwcDevice, 0, HWC_EVENT_VSYNC, 0);

    int hwcEglSurfaceListSize = sizeof(hwc_display_contents_1_t) + sizeof(hwc_layer_1_t);
    hwcEglSurfaceList = (hwc_display_contents_1_t *) malloc(hwcEglSurfaceListSize);
    memset(hwcEglSurfaceList, 0, hwcEglSurfaceListSize);
    hwcEglSurfaceList->retireFenceFd = -1;
    hwcEglSurfaceList->outbuf = 0;
    hwcEglSurfaceList->outbufAcquireFenceFd = -1;
    hwcEglSurfaceList->flags = HWC_GEOMETRY_CHANGED;
    hwcEglSurfaceList->numHwLayers = 1;
    QRect fs(0, 0, size.width(), size.height());
    hwc11_populate_layer(&hwcEglSurfaceList->hwLayers[0], fs, fs, 0, HWC_FRAMEBUFFER_TARGET);
}

void HWC11Thread::cleanup()
{
    free(hwcEglSurfaceList);
    hwcEglSurfaceList = 0;
    free(hwcLayerList);
    hwcLayerList = 0;
    acceptedLayerList = 0;
    HWC_PLUGIN_EXPECT_ZERO(hwc_close_1(hwcDevice));
    hwcDevice = 0;
}

struct _BufferFenceAccessor : public HWComposerNativeWindowBuffer {
    int get() { return fenceFd; }
    void set(int fd) { fenceFd = fd; };
};
static inline int hwc11_getBufferFenceFd(const HWComposerNativeWindowBuffer *b) { return ((_BufferFenceAccessor *) b)->get(); }
static inline void hwc11_setBufferFenceFd(const HWComposerNativeWindowBuffer *b, int fd) { ((_BufferFenceAccessor *) b)->set(fd); }

void HWC11Thread::composeEglSurface()
{
    QSystraceEvent systrace("graphics", "QPA/HWC::composeEglSurface");
    qCDebug(QPA_LOG_HWC, "                                (HWCT) composeEglSurface");
    lock();

    if (size.isNull()) {
        // unlikely bug might happen after destroyWindow
        qCDebug(QPA_LOG_HWC, "                                (HWCT)  - no window surface, aborting");
        unlock();
        return;
    }

    // Grab the current egl surface buffer
    eglSurfaceBuffer = backend->m_eglSurfaceBuffer;
    hwc11_update_layer(hwcEglSurfaceList->hwLayers, hwc11_getBufferFenceFd(eglSurfaceBuffer), eglSurfaceBuffer->handle);
    backend->m_eglSurfaceBuffer = 0;

    // HWC requires retireFenceFd to be unspecified on 'set'
    hwcEglSurfaceList->retireFenceFd = -1;
    hwcEglSurfaceList->flags = HWC_GEOMETRY_CHANGED;

    doComposition(hwcEglSurfaceList);

    hwc11_setBufferFenceFd(eglSurfaceBuffer, hwcEglSurfaceList->hwLayers[0].releaseFenceFd);
    qCDebug(QPA_LOG_HWC, "                                (HWCT)  - egl buffer=%p has release fd=%d (%d)",
            eglSurfaceBuffer->handle, hwcEglSurfaceList->hwLayers[0].releaseFenceFd,
            hwc11_getBufferFenceFd(eglSurfaceBuffer));

    // We need "some" fullscreen buffer to use in checkLayerList's prepare. It
    // doesn't really matter where it comes from, so just use the last frame
    // we swapped.
    lastEglSurfaceBuffer = eglSurfaceBuffer->handle;

    qCDebug(QPA_LOG_HWC, "                                (HWCT)  - composition done, waking up render thread");
    wake();
    unlock();
}

void HWC11Thread::composeAcceptedLayerList()
{
    QSystraceEvent systrace("graphics", "QPA/HWC::composeLayerList");
    qCDebug(QPA_LOG_HWC, "                                (HWCT) composeAcceptedLayerList");

    lock();

    if (size.isNull()) {
        // unlikely bug might happen after destroyWindow
        qCDebug(QPA_LOG_HWC, "                                (HWCT)  - no window surface, aborting");
        unlock();
        return;
    }

    if (!acceptedLayerList) {
        qCDebug(QPA_LOG_HWC, "                                (HWCT)  - layer list has been reset, aborting");
        unlock();
        return;
    }

    // Required by 'set'
    hwcLayerList->retireFenceFd = -1;
    hwcLayerList->flags = HWC_GEOMETRY_CHANGED;

    int actualLayers = 0;
    Q_ASSERT(acceptedLayerList->layerCount);
    while (actualLayers < acceptedLayerList->layerCount && acceptedLayerList->layers[actualLayers].accepted)
        actualLayers++;

    if (acceptedLayerList->eglRenderingEnabled) {
        qCDebug(QPA_LOG_HWC, "                                (HWCT)  - egl surface as layer %d", actualLayers);
        eglSurfaceBuffer = backend->m_eglSurfaceBuffer;
        hwc11_update_layer(&hwcLayerList->hwLayers[actualLayers], hwc11_getBufferFenceFd(eglSurfaceBuffer), eglSurfaceBuffer->handle);
        hwcLayerList->hwLayers[actualLayers].compositionType = HWC_FRAMEBUFFER;
        backend->m_eglSurfaceBuffer = 0;
    }

    // copy the pending layers into our own list
    for (int i=0; i<actualLayers; ++i) {
        // If we're posting the same buffer again, we need to close its
        // release fd and mark it as -1 so we don't send release event back
        // to app after composition...
        buffer_handle_t buffer = (buffer_handle_t) backend->m_layerListBuffers.at(i);
        for (int j=0; j<m_releaseFences.size(); ++j) {
            if (m_releaseFences.at(j).buffer == buffer) {
                int fd = m_releaseFences.at(j).fd;
                if (fd != -1) {
                    qCDebug(QPA_LOG_HWC, "                                (HWCT)  - posting buffer=%p again, closing fd=%d", buffer, fd);
                    close(fd);
                    m_releaseFences[j].fd = -1;
                }
            }
        }
        hwc11_update_layer(&hwcLayerList->hwLayers[i], -1, buffer);
        hwcLayerList->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
    }
    backend->m_layerListBuffers.clear();

    doComposition(hwcLayerList);

    // deal with release fences once composition is over..
    if (acceptedLayerList->eglRenderingEnabled) {
        hwc11_setBufferFenceFd(eglSurfaceBuffer, hwcLayerList->hwLayers[actualLayers].releaseFenceFd);
        qCDebug(QPA_LOG_HWC, "                                (HWCT)  - egl buffer=%p has release fd=%d (%d)",
                eglSurfaceBuffer->handle, hwcLayerList->hwLayers[actualLayers].releaseFenceFd,
                hwc11_getBufferFenceFd(eglSurfaceBuffer));
        lastEglSurfaceBuffer = eglSurfaceBuffer->handle;
    }

    m_releaseFences.resize(actualLayers);
    for (int i=0; i<actualLayers; ++i) {
        const hwc_layer_1_t &l = hwcLayerList->hwLayers[i];
        BufferAndFd entry = { l.handle, l.releaseFenceFd };
        m_releaseFences[i] = entry;
        if (l.releaseFenceFd == -1) {
            qCDebug(QPA_LOG_HWC, "                                (HWCT)  - buffer %p does not have release fence, available right away", l.handle);
            backend->m_bufferAvailableCallback((void *) entry.buffer, backend->m_bufferAvailableCallbackData);
        } else {
            qCDebug(QPA_LOG_HWC, "                                (HWCT)  - buffer %p (fd=%d) stored for later...", l.handle, l.releaseFenceFd);
        }
    }

    qCDebug(QPA_LOG_HWC, "                                (HWCT)  - composition done, waking up render thread");
    wake();
    unlock();
}

void HWC11Thread::doComposition(hwc_display_contents_1_t *dc)
{
    if (QPA_LOG_HWC().isDebugEnabled())
        hwc11_dump_display_contents(dc);

    {
        QSystraceEvent systrace("graphics", "QPA/HWC::prepare");
        HWC_PLUGIN_EXPECT_ZERO(hwcDevice->prepare(hwcDevice, 1, &dc));
    }

    if (QPA_LOG_HWC().isDebugEnabled()) {
        qCDebug(QPA_LOG_HWC, "                                (HWCT)  - after preprare:");
        for (unsigned int i = 0; i<dc->numHwLayers; ++i) {
            qCDebug(QPA_LOG_HWC, "                                (HWCT)    - layer has composition type=%x", dc->hwLayers[i].compositionType);
        }
    }

    qCDebug(QPA_LOG_HWC, "                                (HWCT)  - calling set");
    // if (dc == hwcEglSurfaceList || acceptedLayerList && acceptedLayerList->eglRenderingEnabled)
    {
        QSystraceEvent systrace("graphics", "QPA/HWC::set");
        HWC_PLUGIN_EXPECT_ZERO(hwcDevice->set(hwcDevice, 1, &dc));
    }
    qCDebug(QPA_LOG_HWC, "                                (HWCT)  - set completed..");

    if (QPA_LOG_HWC().isDebugEnabled())
        hwc11_dump_display_contents(dc);

    syncAndCloseOldFences();
    if (dc->retireFenceFd != -1)
        close(dc->retireFenceFd);
}

void HWC11Thread::checkLayerList()
{
    QSystraceEvent systrace("graphics", "QPA/HWC::checkLayerList");
    // Fetch the scheduled layer list. We limit the locking period to the
    // transactional getting of the layerlist only.
    layerListMutex.lock();
    // In the case multiple checks were scheduled by the app, a previous event
    // could have already checked the layerlist, so we can just abort here.
    if (backend->m_scheduledLayerList == 0) {
        layerListMutex.unlock();
        return;
    }
    HwcInterface::LayerList *layerList = backend->m_scheduledLayerList;
    backend->m_scheduledLayerList = 0;
    layerListMutex.unlock();

    if (acceptedLayerList)
        backend->m_releaseLayerListCallback(acceptedLayerList);
    acceptedLayerList = 0;

    int actualLayerCount = 1 + layerList->layerCount + (layerList->eglRenderingEnabled ? 1 : 0);
    int dcSize = sizeof(hwc_display_contents_1_t) + actualLayerCount * sizeof(hwc_layer_1_t);
    hwc_display_contents_1_t *dc = (hwc_display_contents_1_t *) malloc(dcSize);
    memset(dc, 0, dcSize);

    dc->retireFenceFd = -1;
    dc->outbuf = 0;
    dc->outbufAcquireFenceFd = -1;
    dc->flags = HWC_GEOMETRY_CHANGED;
    dc->numHwLayers = actualLayerCount;
    QRect fs(0, 0, size.width(), size.height());

    bool accept = false;

    int layerCount = layerList->layerCount;

    qCDebug(QPA_LOG_HWC, "                                (HWCT) checkLayerList, %d layers, %d%s + HWC_FRAMEBUFFER_TARGET",
            actualLayerCount,
            layerList->layerCount,
            layerList->eglRenderingEnabled ? " + EGL Surface" : "");

    while (!accept && layerCount > 0) {

        for (int i=0; i<layerCount; ++i) {
            const HwcInterface::Layer &l = layerList->layers[i];
            QRect tr(l.tx, l.ty, l.tw, l.th);
            QRect sr(l.sx, l.sy, l.sw, l.sh);
            hwc11_populate_layer(&dc->hwLayers[i], tr, sr, (buffer_handle_t) l.handle, HWC_FRAMEBUFFER);
        }

        dc->numHwLayers = layerCount;

        if (layerList->eglRenderingEnabled) {
            // ### Can lastEglSurfaceBuffer be 0 here?
            hwc11_populate_layer(&dc->hwLayers[layerCount], fs, fs, lastEglSurfaceBuffer, HWC_FRAMEBUFFER);
            ++dc->numHwLayers;
        }

        // Add the dummy fallback HWC_FRAMEBUFFER_TARGET layer. This one has
        // buffer handle 0 as we intend to never render to it and that means
        // 'set' is supposed to ignore it.
        hwc11_populate_layer(&dc->hwLayers[dc->numHwLayers], fs, fs, 0, HWC_FRAMEBUFFER_TARGET);
        ++dc->numHwLayers;

        if (QPA_LOG_HWC().isDebugEnabled())
            hwc11_dump_display_contents(dc);

        if (hwcDevice->prepare(hwcDevice, 1, &dc) == 0) {

            // Iterate over all the layers (excluding the dummy hwc_fb_target)
            // and check that we got HWC_OVERLAY, meaning that composition was
            // supported for that layer. If not, we need to flag it as not
            // possible, remove the last one and try again...
            accept = true;
            for (uint i=0; i<dc->numHwLayers-1; ++i) {
                if (dc->hwLayers[i].compositionType != HWC_OVERLAY) {
                    qCDebug(QPA_LOG_HWC, "                                (HWCT)    - layer %d failed", i);
                    accept = false;
                    break;
                }
            }

            if (!accept) {
                // Not ok, remove one layer and try again. However, this does
                // mean that we need to do egl rendering in addition to our
                // own rendering, so we enable that flag regardless of its own
                // state. This adds another layer, but we also reduce the
                // total count by one so we're still good with the memory we
                // allocated for 'dc'.
                --layerCount;
                layerList->eglRenderingEnabled = true;
                layerList->layers[layerCount].accepted = false;
            }

        } else {
            qCDebug(QPA_LOG_HWC, "                                (HWCT)    - prepare call failed, layerCount=%d, original=%d, egl=%d",
                    layerCount,
                    layerList->layerCount,
                    layerList->eglRenderingEnabled);
            break;
        }
    }

    if (accept) {
        // Flag the accepted ones as such
        for (int i=0; i<layerCount; ++i)
            layerList->layers[i].accepted = true;
        acceptedLayerList = layerList;
        hwcLayerList = dc;

        if (QPA_LOG_HWC().isDebugEnabled()) {
            qCDebug(QPA_LOG_HWC, "                                (HWCT)  - layer list was accepted, %d out of %d, egl=%d",
                    layerCount,
                    layerList->layerCount,
                    layerList->eglRenderingEnabled);
            for (int i=0; i<layerList->layerCount; ++i) {
                const HwcInterface::Layer &l = layerList->layers[i];
                qCDebug(QPA_LOG_HWC, "                                (HWCT)    - %d: %p, t=(%d,%d %dx%d), s=(%d,%d %dx%d) %s", i, l.handle,
                        l.tx, l.ty, l.tw, l.th,
                        l.sx, l.sy, l.sw, l.sh,
                        l.accepted ? "accepted" : "rejected");
            }
        }

    } else {
        qCDebug(QPA_LOG_HWC, "                                (HWCT)  - layer list was not accepted");
        free(dc);
        backend->m_releaseLayerListCallback(layerList);
        Q_ASSERT(acceptedLayerList == 0);
    }
}

void HWC11Thread::syncAndCloseOldFences()
{
    QSystraceEvent systrace("graphics", "QPA/HWC::syncAndCloseOldFences");
    for (int i=0; i<m_releaseFences.size(); ++i) {
        const BufferAndFd &entry = m_releaseFences.at(i);
        if (entry.fd != -1) {
            sync_wait(entry.fd, -1);
            close(entry.fd);
            qCDebug(QPA_LOG_HWC, "                                (HWCT)  - old buffer %p (fd=%d) is released from hwc", entry.buffer, entry.fd);
            backend->m_bufferAvailableCallback((void *) entry.buffer, backend->m_bufferAvailableCallbackData);
        }
    }
    m_releaseFences.clear();
}

bool HWC11Thread::event(QEvent *e)
{
    switch ((int) e->type()) {
    case InitializeAction:
        qCDebug(QPA_LOG_HWC, "                                (HWCT) action: initialize");
        initialize();
        break;
    case CleanupAction:
        qCDebug(QPA_LOG_HWC, "                                (HWCT) action: cleanup");
        cleanup();
        break;
    case EglSurfaceCompositionAction:
        qCDebug(QPA_LOG_HWC, "                                (HWCT) action: egl surface composition");
        composeEglSurface();
        break;
    case DisplayWakeAction:
        qCDebug(QPA_LOG_HWC, "                                (HWCT) action: display wake");
        HWC_PLUGIN_EXPECT_ZERO(hwcDevice->blank(hwcDevice, 0, 0));
        break;
    case DisplaySleepAction:
        qCDebug(QPA_LOG_HWC, "                                (HWCT) action: display sleep");
        HWC_PLUGIN_EXPECT_ZERO(hwcDevice->blank(hwcDevice, 0, 1));
        break;
    case CheckLayerListAction:
        qCDebug(QPA_LOG_HWC, "                                (HWCT) action: check layer list");
        checkLayerList();
        break;
    case ResetLayerListAction:
        qCDebug(QPA_LOG_HWC, "                                (HWCT) action: reset layer list");
        if (acceptedLayerList)
            backend->m_releaseLayerListCallback(acceptedLayerList);
        acceptedLayerList = 0;
        break;
    case LayerListCompositionAction:
        qCDebug(QPA_LOG_HWC, "                                (HWCT) action: layer list composition");
        composeAcceptedLayerList();
        break;
    case ActivateVSyncCallbackAction:
        qCDebug(QPA_LOG_HWC, "                                (HWCT) action: activate vsync callback");
        hwcDevice->eventControl(hwcDevice, 0, HWC_EVENT_VSYNC, 1);
        break;
    case DeactivateVSyncCallbackAction:
        qCDebug(QPA_LOG_HWC, "                                (HWCT) action: deactivate vsync callback");
        hwcDevice->eventControl(hwcDevice, 0, HWC_EVENT_VSYNC, 0);
        break;
    default:
        qCDebug(QPA_LOG_HWC, "                                (HWCT) unknown action: %d", e->type());
        break;
    }
    return QThread::event(e);
}

#endif
