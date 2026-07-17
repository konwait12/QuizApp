#pragma once

#include <QString>

namespace quizapp::platform {

class SharedStoragePlatform final {
public:
    static QString defaultRootPath(const QString &dataRoot);
    static bool hasDirectAccess();
    static bool requiresDirectAccessPermission();
    static bool requestDirectAccess(QString *error = nullptr);
    static bool openInSystemFileManager(const QString &path, QString *error = nullptr);
};

} // namespace quizapp::platform
