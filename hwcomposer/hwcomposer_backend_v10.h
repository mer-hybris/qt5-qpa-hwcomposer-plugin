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

#ifndef HWCOMPOSER_BACKEND_V10_H
#define HWCOMPOSER_BACKEND_V10_H

#ifdef HWC_PLUGIN_HAVE_HWCOMPOSER1_API

#include "hwcomposer_backend.h"

#ifdef HWC_DEVICE_API_VERSION_1_0

#include <pthread.h>

class HwComposerBackend_v10 : public HwComposerBackend {
public:
    HwComposerBackend_v10(hw_module_t *hwc_module, hw_device_t *hw_device);
    virtual ~HwComposerBackend_v10();

    virtual EGLNativeDisplayType display();
    virtual EGLNativeWindowType createWindow(int width, int height);
    virtual void destroyWindow(EGLNativeWindowType window);
    virtual void swap(EGLNativeDisplayType display, EGLSurface surface);
    virtual void sleepDisplay(bool sleep);
    virtual float refreshRate();

private:
    hwc_composer_device_1_t *hwc_device;
    hwc_display_contents_1_t *hwc_list;
    hwc_display_contents_1_t **hwc_mList;
    int hwc_numDisplays;
};

#endif /* HWC_DEVICE_API_VERSION_1_0 */

#endif /* HWC_PLUGIN_HAVE_HWCOMPOSER1_API */

#endif /* HWCOMPOSER_BACKEND_V10_H */
