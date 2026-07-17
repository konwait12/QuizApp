#pragma once

#include "domain/BankImportResult.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace quizapp::repositories {
class IQuestionRepository;
}

namespace quizapp::services {

class BlobStore;

struct BankInstallResult {
    domain::BankImportResult import;
    bool installed = false;
    QString error;
};

class BankInstallService final {
public:
    BankInstallResult installJson(
        const QByteArray &json,
        const QString &sourceKey,
        const BlobStore &blobStore,
        repositories::IQuestionRepository &repository,
        const QString &requiredSourceProvider = {},
        const QStringList &pathOverride = {}) const;
};

} // namespace quizapp::services
