#pragma once

#include "domain/BankImportResult.h"

#include <QString>

namespace quizapp::services {

class BlobStore final {
public:
    explicit BlobStore(QString rootDirectory);

    bool materialize(
        const QVector<domain::PendingMediaReference> &references,
        domain::BankImportPackage *package,
        QString *error = nullptr) const;
    QString rootDirectory() const;
    QString absolutePath(const domain::BlobAsset &asset) const;

private:
    QString rootDirectory_;
};

} // namespace quizapp::services
