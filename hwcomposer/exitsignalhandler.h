/****************************************************************************
**
** Copyright (c) 2021 Open Mobile Platform LLС
** Contact: http://jolla.com/
**
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
****************************************************************************/

#ifndef EXITSIGNALHANDLER_H
#define EXITSIGNALHANDLER_H

#include <QThread>

QT_BEGIN_NAMESPACE

class ExitSignalHandler : public QThread
{
public:
    ExitSignalHandler(QObject *parent = nullptr);
    ~ExitSignalHandler();

protected:
    void run() override;

private:
    static int s_quitSignalFd;

    static void unixSignalHandler(int sig);
    void shutdown();
};

QT_END_NAMESPACE

#endif // EXITSIGNALHANDLER_H
