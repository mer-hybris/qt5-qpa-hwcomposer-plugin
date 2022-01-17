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

#ifndef HWCOMPOSER_BACKEND_V11_H
#define HWCOMPOSER_BACKEND_V11_H

#ifdef HWC_PLUGIN_HAVE_HWCOMPOSER1_API

#include "hwcomposer_backend.h"

// libhybris access to the native hwcomposer window
#include <hwcomposer_window.h>

#include <QBasicTimer>

// #define QPA_DEBUG_FENCES

// Helper class that takes care of waiting on and closing a set
// of file descriptors
class RetireFencePool {
public:
    RetireFencePool(bool waitOnRetireFence)
        : m_fds(), m_waitOnRetireFence(waitOnRetireFence)
    {
    }

    ~RetireFencePool()
    {
        for (auto fd: m_fds) {
#ifdef QPA_DEBUG_FENCES
            qDebug() << "Waiting and closing retire fence fd:" << fd;
#endif
            if (m_waitOnRetireFence) sync_wait(fd, -1);
            close(fd);
        }
    }

    void consume(int &fd)
    {
        if (fd != -1) {
            m_fds.push_back(fd);
            fd = -1;
        }
    }

private:
    std::vector<int> m_fds;
    bool m_waitOnRetireFence;
};

class HwcProcs_v11;
class QWindow;
class HwComposerContent_v11;

class HwComposerBackend_v11 : public QObject, public HwComposerBackend {
public:
    HwComposerBackend_v11(hw_module_t *hwc_module, hw_device_t *hw_device, void *libminisf, int num_displays);
    virtual ~HwComposerBackend_v11();

    virtual EGLNativeDisplayType display();
    virtual EGLNativeWindowType createWindow(int width, int height);
    virtual void destroyWindow(EGLNativeWindowType window);
    virtual void swap(EGLNativeDisplayType display, EGLSurface surface);
    virtual void blankDisplay(int display, bool blank);
    virtual void sleepDisplay(bool sleep);
    virtual float refreshRate();
    virtual bool getScreenSizes(int *width, int *height, float *physical_width, float *physical_height);

    virtual bool requestUpdate(QEglFSWindow *window) Q_DECL_OVERRIDE;

    void timerEvent(QTimerEvent *) Q_DECL_OVERRIDE;
    void handleVSyncEvent();
    bool event(QEvent *e) Q_DECL_OVERRIDE;

    // Present method that does the buffer swapping, returns the releaseFenceFd
    int present(RetireFencePool *pool, buffer_handle_t handle, int acquireFenceFd);

    void screenPlugged();

private:
    int getSingleAttribute(uint32_t attribute);
    hwc_composer_device_1_t *hwc_device;
    hwc_display_contents_1_t *hwc_list;
    hwc_display_contents_1_t **hwc_mList;
    uint32_t hwc_version;
    int num_displays;
    bool m_screenAttachedGeometryChanged;

    bool m_displayOff;
    QBasicTimer m_deliverUpdateTimeout;
    QBasicTimer m_vsyncTimeout;
    QSet<QWindow *> m_pendingUpdate;
    HwcProcs_v11 *procs;

    int width;
    int height;
    HwComposerContent_v11 *content;
};

#endif /* HWC_PLUGIN_HAVE_HWCOMPOSER1_API */

#endif /* HWCOMPOSER_BACKEND_V11_H */
