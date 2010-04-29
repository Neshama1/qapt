/***************************************************************************
 *   Copyright © 2010 Jonathan Thomas <echidnaman@kubuntu.org>             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2 of        *
 *   the License or (at your option) version 3 or any later version        *
 *   accepted by the membership of KDE e.V. (or its successor approved     *
 *   by the membership of KDE e.V.), which shall act as a proxy            *
 *   defined in Section 14 of version 3 of the license.                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "workerinstallprogress.h"

#include <QtCore/QStringList>
#include <QtCore/QFile>

#include <apt-pkg/error.h>

#include <sys/statvfs.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <errno.h>

#include <iostream>
#include <stdlib.h>

using namespace std;

WorkerInstallProgress::WorkerInstallProgress(QObject* parent)
        : QObject(parent)
{
    //TODO: Debconf, apt-listchanges
    setenv("DEBIAN_FRONTEND", "noninteractive", 1);
    setenv("APT_LISTCHANGES_FRONTEND", "none", 1);

    m_startCounting = false;
}

WorkerInstallProgress::~WorkerInstallProgress()
{
}

pkgPackageManager::OrderResult WorkerInstallProgress::start(pkgPackageManager *pm)
{
    pkgPackageManager::OrderResult res;

    res = pm->DoInstallPreFork();
    if (res == pkgPackageManager::Failed) {
        return res;
    }

    m_child_id = fork();
    int readFromChildFD[2];

    if (m_child_id < 0) {
        return res;
    } else if (m_child_id == 0) {
        pipe(readFromChildFD);

        res = pm->DoInstallPostFork(readFromChildFD[1]);

        // dump errors into cerr (pass it to the parent process)
        _error->DumpErrors();

        ::close(readFromChildFD[0]);
        ::close(readFromChildFD[1]);

        _exit(res);
    }

    // make it nonblocking
    fcntl(readFromChildFD[0], F_SETFL, O_NONBLOCK);

    // Check if the child died
    int ret;
    while (waitpid(m_child_id, &ret, WNOHANG) == 0) {
        updateInterface(readFromChildFD[0]);
    }

    ::close(readFromChildFD[0]);

    return res;
}

void WorkerInstallProgress::updateInterface(int fd)
{
    char buf[2];
    static char line[1024] = "";

    while (1) {
        // This algorithm should be improved (it's the same as the rpm one ;)
        int len = read(fd, buf, 1);

        // nothing was read
        if (len < 1) {
            break;
        }

        if (buf[0] == '\n') {
            //cout << "got line: " << line << endl;
            QStringList list = QString::fromStdString(line).split(":");
            QString status = list[0].simplified();
            QString package = list[1].simplified();
            QString percent = list[2].simplified();
            QString str = list[3];

            if (package.isNull() || status.isNull()) {
                continue;
            }

            if (status.contains("pmerror")) {
                //TODO: emit error (str)
            } else if (status.contains("pmconffile")) {
                //TODO: Conffile handling
            } else {
                m_startCounting = true;
            }

            int percentage = (percent.toInt()/100);

            emit transactionProgress(package, status, percentage);
            // clean-up
            line[0] = 0;
        } else {
            buf[1] = 0;
            strcat(line, buf);
        }
    }
}


