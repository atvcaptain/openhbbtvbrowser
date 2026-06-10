#ifndef HARDWAREPROFILE_H
#define HARDWAREPROFILE_H

#include <QString>
#include <QStringList>

class HardwareProfile
{
public:
    static void applyEnvironment(int argc, char *argv[]);
    static QString remoteDevice(int argc, char *argv[]);
    static bool filterRemoteNavigationKeys(int argc, char *argv[]);
    static QString modelSummary();

private:
    static QString argumentValue(int argc, char *argv[], const QString &name);
    static QString environmentValue(const char *name);
    static QString readFirstExistingFile(const QStringList &paths);
    static QString readHardwareText();
    static QString detectPlatform();
    static QString detectRemoteDevice();
    static bool isTruthy(const QString &value);
    static bool isFalsy(const QString &value);
};

#endif // HARDWAREPROFILE_H
