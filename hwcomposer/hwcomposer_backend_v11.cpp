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

#ifdef HWC_PLUGIN_HAVE_HWCOMPOSER1_API

class HWC11Thread;


class HWC11WindowSurface : public HWComposerNativeWindow
{
protected:
    void present(HWComposerNativeWindowBuffer *buffer);

public:
    unsigned int width() const { return HWComposerNativeWindow::width(); }
    unsigned int height() const { return HWComposerNativeWindow::height(); }
    HWC11WindowSurface(unsigned int width, unsigned int height, unsigned int format);

    HWComposerNativeWindowBuffer *buffer;
    HWC11Thread *thread;
};


class HWC11Thread : public QThread
{
public:
    enum Action {
        InitializeAction = QEvent::User,
        CleanupAction,
        BufferReadyAction,
        DisplaySleepAction,
        DisplayWakeAction,
    };

    HWC11Thread();

    void swap();
    void initialize();
    void cleanup();
    void post(Action a) { QCoreApplication::postEvent(this, new QEvent((QEvent::Type) a)); }

    bool event(QEvent *e);

    inline void lock() { mutex.lock(); }
    inline void unlock() { mutex.unlock(); }
    inline void wait() { condition.wait(&mutex); }
    inline void wake() { condition.wakeOne(); }

    HWC11WindowSurface *windowSurface;
    HWComposerNativeWindowBuffer *buffer;
    hwc_composer_device_1_t *device;
    hwc_display_contents_1_t *eglSurfaceList;
    QMutex mutex;
    QWaitCondition condition;
    int frameFence;
};


HWC11WindowSurface::HWC11WindowSurface(unsigned int width, unsigned int height, unsigned int format)
    : HWComposerNativeWindow(width, height, format)
    , buffer(0)
    , thread(0)
{
}

/* Sets the buffer as the current front buffer to be displayed through the
   HWC. The HWC will pick up the buffer and set it to 0.

   If there already is a buffer pending for display, this function will block
   until the current buffer has been picked up. As HwcWindowSurfaceNativeWindow
   has two buffers by default, this allows us to queue up one buffer before
   rendering is blocked on the EGL render thread.
 */
void HWC11WindowSurface::present(HWComposerNativeWindowBuffer *b)
{
    qCDebug(QPA_LOG_HWC, "present: %p (%p), current=%p", b, b->handle, buffer);
    thread->lock();
    if (buffer != 0) {
        qCDebug(QPA_LOG_HWC, " - buffer already pending, waiting for hwc to pick it up");
        thread->wait();
        qCDebug(QPA_LOG_HWC, " - buffer picked up, setting front buffer %p, current=%p", b, buffer);
    }
    buffer = b;
    thread->post(HWC11Thread::BufferReadyAction);
    thread->unlock();
}


HwComposerBackend_v11::HwComposerBackend_v11(hw_module_t *hwc_module, hw_device_t *hw_device, int num_displays)
    : HwComposerBackend(hwc_module)
{
    m_thread = new HWC11Thread();
    m_thread->moveToThread(m_thread);
    m_thread->device = (hwc_composer_device_1_t *) hw_device;
    m_thread->post(HWC11Thread::InitializeAction);
}

HwComposerBackend_v11::~HwComposerBackend_v11()
{
    // Stop the compositor thread
    m_thread->post(HWC11Thread::CleanupAction);
    m_thread->quit();
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
    Q_ASSERT(!m_thread->windowSurface);
    HWC11WindowSurface *window = new HWC11WindowSurface(width, height, HAL_PIXEL_FORMAT_RGBA_8888);
    window->thread = m_thread;
    m_thread->windowSurface = window;
    m_thread->start();
    return (EGLNativeWindowType) static_cast<ANativeWindow *>(window);
}

void
HwComposerBackend_v11::destroyWindow(EGLNativeWindowType window)
{
    qCDebug(QPA_LOG_HWC, "destroyWindow");
    Q_UNUSED(window); // avoid warning in release build without the assert..
    Q_ASSERT((HWC11WindowSurface *) static_cast<ANativeWindow *>((void *)window) == m_thread->windowSurface);

    m_thread->lock();
    m_thread->windowSurface = 0;
    m_thread->unlock();

    delete (HWC11WindowSurface *) static_cast<ANativeWindow *>((void *)window);
}

void
HwComposerBackend_v11::swap(EGLNativeDisplayType display, EGLSurface surface)
{
    // TODO: Wait for vsync?
    qCDebug(QPA_LOG_HWC, "eglSwapBuffers");
    eglSwapBuffers(display, surface);
}

void
HwComposerBackend_v11::sleepDisplay(bool sleep)
{
    qCDebug(QPA_LOG_HWC, "sleep: %d", sleep);
    m_thread->post(sleep ? HWC11Thread::DisplaySleepAction : HWC11Thread::DisplayWakeAction);
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



static void hwc11_callback_vsync(const struct hwc_procs *, int, int64_t timestamp)
{
    qCDebug(QPA_LOG_HWC, "callback_vsync");
}

static void hwc11_callback_invalidate(const struct hwc_procs *)
{
    qCDebug(QPA_LOG_HWC, "callback_invalidate");
}

static void hwc11_callback_hotplug(const struct hwc_procs *, int, int)
{
    qCDebug(QPA_LOG_HWC, "callback_hotplug");
}

HWC11Thread::HWC11Thread()
    : windowSurface(0)
    , buffer(0)
    , device(0)
    , eglSurfaceList(0)
    , frameFence(-1)
{
}

static void hwc11_populate_layer(hwc_layer_1_t *layer, int width, int height, buffer_handle_t handle, int32_t type)
{
    layer->handle = handle;
    layer->hints = 0;
    layer->flags = 0;
    layer->compositionType = type;
    layer->blending = HWC_BLENDING_NONE;
    layer->transform = 0;
    layer->acquireFenceFd = -1;
    layer->releaseFenceFd = -1;
#ifdef HWC_DEVICE_API_VERSION_1_2
    layer->planeAlpha = 0xff;
#endif
#ifdef HWC_DEVICE_API_VERSION_1_3
    layer->sourceCropf.left = 0.0;
    layer->sourceCropf.top = 0.0;
    layer->sourceCropf.right = width;
    layer->sourceCropf.bottom = height;
#else
    layer->sourceCrop.left = 0;
    layer->sourceCrop.top = 0;
    layer->sourceCrop.right = width;
    layer->sourceCrop.bottom = height;
#endif
    layer->displayFrame.left = 0;
    layer->displayFrame.top = 0;
    layer->displayFrame.right = width;
    layer->displayFrame.bottom = height;
    layer->visibleRegionScreen.numRects = 1;
    layer->visibleRegionScreen.rects = &layer->displayFrame;
}

void HWC11Thread::initialize()
{
    qCDebug(QPA_LOG_HWC, "                (RT) initialize");
    Q_ASSERT(windowSurface);

    hwc_procs *procs = new hwc_procs();
    procs->invalidate = hwc11_callback_invalidate;
    procs->hotplug = hwc11_callback_hotplug;
    procs->vsync = hwc11_callback_vsync;
    device->registerProcs(device, procs);
    device->eventControl(device, 0, HWC_EVENT_VSYNC, 0);

    int eglSurfaceListSize = sizeof(hwc_display_contents_1_t) + sizeof(hwc_layer_1_t);
    eglSurfaceList = (hwc_display_contents_1_t *) malloc(eglSurfaceListSize);
    memset(eglSurfaceList, 0, eglSurfaceListSize);
    eglSurfaceList->retireFenceFd = -1;
    eglSurfaceList->outbuf = 0;
    eglSurfaceList->outbufAcquireFenceFd = -1;
    eglSurfaceList->flags = HWC_GEOMETRY_CHANGED;
    eglSurfaceList->numHwLayers = 1;
    hwc11_populate_layer(&eglSurfaceList->hwLayers[0], windowSurface->width(), windowSurface->height(), 0, HWC_FRAMEBUFFER_TARGET);
}

void HWC11Thread::cleanup()
{
    free(eglSurfaceList);
    eglSurfaceList = 0;
    HWC_PLUGIN_EXPECT_ZERO(hwc_close_1(device));
    device = 0;
}

struct _BufferFenceAccessor : public HWComposerNativeWindowBuffer {
    int get() { return fenceFd; }
    void set(int fd) { fenceFd = fd; };
};
static inline int hwc11_getBufferFenceFd(const HWComposerNativeWindowBuffer *b) { return ((_BufferFenceAccessor *) b)->get(); }
static inline void hwc11_setBufferFenceFd(const HWComposerNativeWindowBuffer *b, int fd) { ((_BufferFenceAccessor *) b)->set(fd); }

void HWC11Thread::swap()
{
    qCDebug(QPA_LOG_HWC, "                (RT) swap");
    lock();

    if (!windowSurface) {
        // unlikely bug might happen after destroyWindow
        qCDebug(QPA_LOG_HWC, "                (RT)  - no window surface, aborting");
        unlock();
        return;
    }

    Q_ASSERT(windowSurface->buffer);
    buffer = windowSurface->buffer;

    eglSurfaceList->retireFenceFd = -1;

    // copy the buffer state into our list
    hwc_layer_1_t &l = eglSurfaceList->hwLayers[0];
    l.handle = buffer->handle;
    l.acquireFenceFd = hwc11_getBufferFenceFd(buffer);
    l.releaseFenceFd = -1;
    l.hints = HWC_GEOMETRY_CHANGED;

    if (QPA_LOG_HWC().isDebugEnabled()) {
        qCDebug(QPA_LOG_HWC, "                (RT)  - %d buffers (including HWC_FRAMEBUFFER_TARGET)",  eglSurfaceList->numHwLayers);
        qCDebug(QPA_LOG_HWC, "                (RT)  - displayContents, retireFence=%d, outbuf=%p, outAcqFence=%d, flags=%x, numLayers=%d",
                eglSurfaceList->retireFenceFd,
                eglSurfaceList->outbuf,
                eglSurfaceList->outbufAcquireFenceFd,
                (int) eglSurfaceList->flags,
                (int) eglSurfaceList->numHwLayers);
        qCDebug(QPA_LOG_HWC, "                (RT)    - layer comp=%x, hints=%x, flags=%x, handle=%p, transform=%x, blending=%x, "
                "src=(%d %d - %d %d), dst=(%d %d - %d %d), afd=%d, rfd=%d, a=%d",
                l.compositionType, l.hints, l.flags, l.handle, l.transform, l.blending,
                (int) l.sourceCropf.left, (int) l.sourceCropf.top, (int) l.sourceCropf.right, (int) l.sourceCropf.bottom,
                l.displayFrame.left, l.displayFrame.top, l.displayFrame.right, l.displayFrame.bottom,
                l.acquireFenceFd, l.releaseFenceFd, l.planeAlpha);
        for (unsigned int j=0; j<l.visibleRegionScreen.numRects; ++j) {
            qCDebug(QPA_LOG_HWC, "                (RT)      - region (%d %d - %d %d)",
                    l.visibleRegionScreen.rects[j].left,
                    l.visibleRegionScreen.rects[j].top,
                    l.visibleRegionScreen.rects[j].right,
                    l.visibleRegionScreen.rects[j].bottom
                   );
        }
    }

    int prepResult = device->prepare(device, 1, &eglSurfaceList);
    if (prepResult != 0) {
        qDebug("prepare() failed... %x",  prepResult);
        return;
    }

    if (QPA_LOG_HWC().isDebugEnabled()) {
        qCDebug(QPA_LOG_HWC, "                (RT)  - after preprare:");
        for (unsigned int i = 0; i<eglSurfaceList->numHwLayers; ++i) {
            qCDebug(QPA_LOG_HWC, "                (RT)    - layer has composition type=%x", eglSurfaceList->hwLayers[i].compositionType);
        }
    }

    int setResult = device->set(device, 1, &eglSurfaceList);
    if (setResult != 0) {
        qDebug("set() failed... %x",  setResult);
        return;
    }

    if (frameFence != -1) {
        sync_wait(frameFence, -1);
        close(frameFence);
        qCDebug(QPA_LOG_HWC, "                (RT)  --- waited");
    }
    frameFence = eglSurfaceList->retireFenceFd;
    if (frameFence != -1)
        qCDebug(QPA_LOG_HWC, "                (RT)  - frame fence: %d", frameFence);

    hwc11_setBufferFenceFd(buffer, eglSurfaceList->hwLayers[0].releaseFenceFd);

    eglSurfaceList->flags = 0;

    windowSurface->buffer = 0;

    qCDebug(QPA_LOG_HWC, "                (RT)  - composition done, waking up render thread");
    wake();
    unlock();
}

bool HWC11Thread::event(QEvent *e)
{
    qCDebug(QPA_LOG_HWC, "                (RT) action: %d", e->type());;
    switch ((int) e->type()) {
    case InitializeAction:
        initialize();
        break;
    case CleanupAction:
        cleanup();
        break;
    case BufferReadyAction:
        swap();
        break;
    case DisplayWakeAction:
        HWC_PLUGIN_EXPECT_ZERO(device->blank(device, 0, 0));
        break;
    case DisplaySleepAction:
        HWC_PLUGIN_EXPECT_ZERO(device->blank(device, 0, 1));
        break;
    default:
        qCDebug(QPA_LOG_HWC, "unhandled event type: %d", e->type());
        break;
    }
    return QThread::event(e);
}

#endif /* HWC_PLUGIN_HAVE_HwcWindowSurface1_API */
