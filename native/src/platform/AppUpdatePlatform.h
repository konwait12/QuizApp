#pragma once

#include <QString>

namespace quizapp::platform {

class AppUpdatePlatform
{
public:
    static bool installDownloadedPackage(const QString &path, QString *error = nullptr);
};

} // namespace quizapp::platform
