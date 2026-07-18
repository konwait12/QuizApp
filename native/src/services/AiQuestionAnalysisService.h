#pragma once

#include "domain/AiRecord.h"
#include "domain/Question.h"
#include "services/AiConfigService.h"

#include <QJsonObject>
#include <QObject>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace quizapp::services {

class AiQuestionAnalysisService final : public QObject {
    Q_OBJECT

public:
    explicit AiQuestionAnalysisService(
        QString databasePath,
        QString dataRoot,
        QObject *parent = nullptr);

    std::optional<domain::AiRecord> cachedRecord(
        const domain::Question &question,
        QString *error = nullptr) const;
    static bool isStale(
        const domain::AiRecord &record,
        const domain::Question &question);
    static QJsonObject requestBody(
        const domain::Question &question,
        const AiConfiguration &configuration,
        const QStringList &imageDataUrls = {});
    static QString parseResponse(const QByteArray &payload, QString *error = nullptr);

public slots:
    void analyze(const domain::Question &question, bool replaceCached = false);
    void cancel();

signals:
    void analysisStarted(const QUuid &questionId);
    void analysisFinished(const domain::AiRecord &record);
    void analysisFailed(const QUuid &questionId, const QString &message);
    void analysisCancelled(const QUuid &questionId);

private:
    QStringList imageDataUrls(
        const domain::Question &question,
        const AiConfiguration &configuration) const;
    bool saveRecord(const domain::AiRecord &record, QString *error) const;
    void finishReply();

    QString databasePath_;
    QString dataRoot_;
    QNetworkAccessManager *network_ = nullptr;
    QNetworkReply *reply_ = nullptr;
    domain::Question pendingQuestion_;
    AiConfiguration pendingConfiguration_;
};

} // namespace quizapp::services
