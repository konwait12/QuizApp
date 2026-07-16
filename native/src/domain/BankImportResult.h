#pragma once

#include "domain/QuestionBank.h"

#include <QString>
#include <QUuid>
#include <QVector>

#include <optional>

namespace quizapp::domain {

enum class ImportDiagnosticSeverity {
    Info,
    Warning,
    Error,
};

struct ImportDiagnostic {
    ImportDiagnosticSeverity severity = ImportDiagnosticSeverity::Info;
    QString code;
    QString message;
    qsizetype questionIndex = -1;
    QString sourceQuestionId;
};

enum class ImportedMediaRole {
    Question,
    Explanation,
};

struct PendingMediaReference {
    QUuid questionId;
    ImportedMediaRole role = ImportedMediaRole::Question;
    QString source;
    int sortOrder = 0;
};

struct BankImportResult {
    std::optional<BankImportPackage> package;
    QVector<ImportDiagnostic> diagnostics;
    QVector<PendingMediaReference> pendingMedia;
    qsizetype sourceQuestionCount = 0;
    qsizetype acceptedQuestionCount = 0;
    qsizetype repairedQuestionCount = 0;
    qsizetype rejectedQuestionCount = 0;

    bool succeeded() const
    {
        return package.has_value() && rejectedQuestionCount == 0;
    }
};

} // namespace quizapp::domain

