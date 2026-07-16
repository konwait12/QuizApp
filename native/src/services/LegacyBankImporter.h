#pragma once

#include "domain/BankImportResult.h"

#include <QByteArray>
#include <QString>

namespace quizapp::services {

class LegacyBankImporter final {
public:
    domain::BankImportResult importJson(
        const QByteArray &json,
        const QString &sourceKey) const;
};

} // namespace quizapp::services

