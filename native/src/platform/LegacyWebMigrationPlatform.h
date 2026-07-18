#pragma once

#include <QString>

namespace quizapp::platform {

enum class LegacyWebExportStatus {
    Unavailable,
    Idle,
    Running,
    Complete,
    Failed,
    NoData,
};

class LegacyWebMigrationPlatform final {
public:
    static bool hasSourceData();
    static bool start(const QString &sourceVersion, QString *error = nullptr);
    static LegacyWebExportStatus status();
    static QString resultPath();
    static QString error();
    static void clearResult();
};

} // namespace quizapp::platform
