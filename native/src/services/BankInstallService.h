#pragma once

#include "domain/BankImportResult.h"

#include <QByteArray>
#include <QString>

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
        const QString &requiredSourceProvider = {}) const;
};

} // namespace quizapp::services
