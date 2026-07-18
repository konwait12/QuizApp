#pragma once

#include <QString>

namespace quizapp::platform {

enum class BackupDocumentState { Idle = 0, Pending = 1, Succeeded = 2, Failed = 3, Cancelled = 4 };
enum class BackupDocumentKind { None = 0, Import = 1, Export = 2 };

struct BackupDocumentResult {
    BackupDocumentState state = BackupDocumentState::Idle;
    BackupDocumentKind kind = BackupDocumentKind::None;
    QString path;
    QString error;
};

class BackupDocumentPlatform final {
public:
    static bool openDocument(const QString &destinationPath, QString *error = nullptr);
    static bool createDocument(
        const QString &sourcePath,
        const QString &suggestedName,
        QString *error = nullptr);
    static BackupDocumentResult result();
    static void clearResult();
};

} // namespace quizapp::platform
