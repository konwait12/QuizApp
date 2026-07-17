#pragma once

#include "domain/BankImportResult.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace quizapp::services {

class LegacyBankImporter final {
public:
    domain::BankImportResult importJson(
        const QByteArray &json,
        const QString &sourceKey,
        const QStringList &pathOverride = {}) const;
};

} // namespace quizapp::services
