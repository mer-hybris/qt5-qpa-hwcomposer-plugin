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

#include "exitsignalhandler.h"

#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QSocketNotifier>
#include <QDebug>

QT_BEGIN_NAMESPACE

int ExitSignalHandler::s_quitSignalFd = -1;

ExitSignalHandler::ExitSignalHandler(QObject *parent)
    : QThread(parent)
{
}

ExitSignalHandler::~ExitSignalHandler()
{
    quit();
}

void ExitSignalHandler::unixSignalHandler(int sig)
{
    uint64_t a = 1;
    ::write(s_quitSignalFd, &a, sizeof(a));
    qDebug() << "Exiting on signal:" << sig;
}

void ExitSignalHandler::shutdown()
{
    Q_ASSERT_X(qApp->thread() == QThread::currentThread(), "shutdown", "exit from non GUI thread");

    QCoreApplication::exit(0);
    quit();
    qDebug() << "Signal handled - now exit";
}

void ExitSignalHandler::run()
{
    s_quitSignalFd = ::eventfd(0, 0);
    if (s_quitSignalFd == -1) {
        qWarning("Failed to create eventfd object for signal handling");
        return;
    }

    auto socketNotifier = new QSocketNotifier(s_quitSignalFd, QSocketNotifier::Read);
    connect(socketNotifier, &QSocketNotifier::activated, qApp, [this] {
        uint64_t a;
        ::read(s_quitSignalFd, &a, sizeof(a));
        shutdown();
    });

    // We need to catch the SIGTERM and SIGINT signals, so that we can do a
    // proper shutdown of Qt and the plugin, and avoid crashes, hangs and
    // reboots in cases where we don't properly close the hwcomposer.
    struct sigaction action;
    action.sa_handler = unixSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_flags |= SA_RESTART;
    if (sigaction(SIGTERM, &action, NULL))
        qWarning("Failed to set up SIGINT handling");
    if (sigaction(SIGINT, &action, NULL))
        qWarning("Failed to set up SIGTERM handling");

    exec();

    delete socketNotifier;
    ::close(s_quitSignalFd);
    s_quitSignalFd = -1;
}

QT_END_NAMESPACE
