#pragma once

#include <QString>

#include <functional>

namespace quizapp::services {

struct DirectoryInstallResult {
    int discoveredSections = 0;
    int installedSections = 0;
    qsizetype installedQuestions = 0;
    bool canceled = false;
    QString error;
};

class XiaoyiDirectoryInstallService final {
public:
    using ProgressCallback = std::function<bool(int current, int total, const QString &sourceKey)>;

    DirectoryInstallResult install(
        const QString &inputDirectory,
        const QString &databasePath,
        const QString &dataRoot,
        const ProgressCallback &progress = {}) const;
};

} // namespace quizapp::services
