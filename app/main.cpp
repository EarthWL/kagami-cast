// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // No organization name on purpose: the data folder is then simply
    // %LOCALAPPDATA%\KagamiCast (settings are an explicit .ini file there).
    QApplication::setApplicationName(QStringLiteral("KagamiCast"));
    QApplication::setApplicationDisplayName(QStringLiteral(APP_DISPLAY_NAME));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    // Per-user writable folder for settings, log and the engine's key/registry
    // (the install folder under Program Files is read-only for the user).
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDir);

    // One instance only: a second uxplay would fight over the same name and ports.
    // A lock left by a crashed instance is detected (dead PID) and taken over.
    QLockFile lock(dataDir + QStringLiteral("/instance.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(200)) {
        if (lock.error() == QLockFile::LockFailedError) {
            QMessageBox::information(nullptr, QStringLiteral(APP_DISPLAY_NAME),
                                     QStringLiteral("โปรแกรมเปิดอยู่แล้ว — ดูที่แถบงาน (taskbar)"));
            return 0;
        }
        // PermissionError / UnknownError: the data folder itself is the problem
        QMessageBox::warning(nullptr, QStringLiteral(APP_DISPLAY_NAME),
                             QStringLiteral("เปิดโปรแกรมไม่ได้: เขียนไฟล์ในโฟลเดอร์นี้ไม่ได้\n%1")
                                 .arg(QDir::toNativeSeparators(dataDir)));
        return 1;
    }

    MainWindow w(dataDir);
    w.show();
    return app.exec();
}
