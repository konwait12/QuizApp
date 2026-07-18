#include "services/AiQuestionAnalysisService.h"

#include "storage/Database.h"
#include "storage/SqliteAiRecordRepository.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>

namespace quizapp::services {
namespace {

constexpr auto kRecordType = "question_analysis";

QString connectionName()
{
    return QStringLiteral("ai-analysis-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString questionTypeText(domain::QuestionType type)
{
    switch (type) {
    case domain::QuestionType::Single: return QStringLiteral("单选题");
    case domain::QuestionType::Multiple: return QStringLiteral("多选题");
    case domain::QuestionType::Boolean: return QStringLiteral("判断题");
    case domain::QuestionType::Subjective: return QStringLiteral("主观题");
    }
    return QStringLiteral("未知题型");
}

QString responseError(const QByteArray &payload, const QString &fallback)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (document.isObject()) {
        const QJsonObject root = document.object();
        QString message = root.value(QStringLiteral("error")).toObject()
                              .value(QStringLiteral("message")).toString();
        if (message.isEmpty()) message = root.value(QStringLiteral("message")).toString();
        if (!message.trimmed().isEmpty()) return message.trimmed().left(500);
    }
    return fallback;
}

} // namespace

AiQuestionAnalysisService::AiQuestionAnalysisService(
    QString databasePath,
    QString dataRoot,
    QObject *parent)
    : QObject(parent)
    , databasePath_(std::move(databasePath))
    , dataRoot_(std::move(dataRoot))
    , network_(new QNetworkAccessManager(this))
{
}

std::optional<domain::AiRecord> AiQuestionAnalysisService::cachedRecord(
    const domain::Question &question,
    QString *error) const
{
    if (error) error->clear();
    if (databasePath_.isEmpty() || question.id.isNull()) return std::nullopt;
    storage::Database database(connectionName());
    if (!database.open(databasePath_, error) || !database.migrate(error)) {
        return std::nullopt;
    }
    storage::SqliteAiRecordRepository repository(database.connection());
    return repository.find(
        QString::fromLatin1(kRecordType),
        question.id.toString(QUuid::WithoutBraces),
        error);
}

bool AiQuestionAnalysisService::isStale(
    const domain::AiRecord &record,
    const domain::Question &question)
{
    return record.sourceHash != question.contentHash;
}

QJsonObject AiQuestionAnalysisService::requestBody(
    const domain::Question &question,
    const AiConfiguration &configuration,
    const QStringList &imageDataUrls)
{
    QStringList options;
    for (qsizetype index = 0; index < question.options.size(); ++index) {
        options.append(QStringLiteral("%1. %2")
            .arg(QChar(static_cast<char16_t>(u'A' + index)), question.options.at(index)));
    }
    const QString userText = QStringLiteral(
        "题型：%1\n题干：%2\n全部选项：\n%3\n结构化正确答案：%4\n题库内置解析：%5\n\n"
        "请给出清晰、可核对的解析，说明正确选项依据和其他选项的问题。"
        "如果题库资料不足，请明确指出，不要改写题库给定答案。")
        .arg(questionTypeText(question.type),
             question.prompt.trimmed(),
             options.isEmpty() ? QStringLiteral("（无选项）") : options.join(u'\n'),
             question.correctAnswer.trimmed().isEmpty()
                 ? QStringLiteral("（未提供）") : question.correctAnswer.trimmed(),
             question.builtinExplanation.text.trimmed().isEmpty()
                 ? QStringLiteral("（暂无内置解析）")
                 : question.builtinExplanation.text.trimmed());

    const QString defaultSystem = QStringLiteral(
        "你是严谨的题目解析教师。收到题型、题干、全部选项、结构化正确答案和题库已有解析。"
        "正确答案是题库给定答案，不得擅自改写。内置解析与 AI 分析是不同来源；若发现疑点，"
        "只能标明疑点并建议核对题库。使用简洁的 Markdown 中文作答。");
    QString system = defaultSystem;
    if (!configuration.customSystemPrompt.trimmed().isEmpty()) {
        system += QStringLiteral("\n\n用户追加要求：")
            + configuration.customSystemPrompt.trimmed();
    }

    QJsonValue userContent(userText);
    if (!imageDataUrls.isEmpty()) {
        QJsonArray parts;
        parts.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"), userText},
        });
        for (const QString &url : imageDataUrls) {
            parts.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("image_url")},
                {QStringLiteral("image_url"), QJsonObject{
                    {QStringLiteral("url"), url},
                    {QStringLiteral("detail"), QStringLiteral("auto")},
                }},
            });
        }
        userContent = parts;
    }

    return QJsonObject{
        {QStringLiteral("model"), configuration.model},
        {QStringLiteral("messages"), QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                        {QStringLiteral("content"), system}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), userContent}},
        }},
        {QStringLiteral("max_tokens"), configuration.maxTokens},
        {QStringLiteral("temperature"), configuration.temperature},
        {QStringLiteral("stream"), false},
    };
}

QString AiQuestionAnalysisService::parseResponse(const QByteArray &payload, QString *error)
{
    if (error) error->clear();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("AI 返回内容不是有效 JSON");
        return {};
    }
    const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
    const QString content = choices.isEmpty() ? QString()
        : choices.first().toObject().value(QStringLiteral("message"))
              .toObject().value(QStringLiteral("content")).toString().trimmed();
    if (content.isEmpty() && error) *error = QStringLiteral("AI 没有返回解析内容");
    return content;
}

void AiQuestionAnalysisService::analyze(
    const domain::Question &question,
    bool replaceCached)
{
    if (reply_) cancel();
    if (question.id.isNull()) {
        emit analysisFailed(question.id, QStringLiteral("当前题目无有效标识"));
        return;
    }
    QString error;
    QSettings settings;
    const AiConfiguration configuration = AiConfigService::load(settings, &error);
    if (!error.isEmpty() || !AiConfigService::validate(configuration, true, &error)) {
        emit analysisFailed(question.id, error.isEmpty()
            ? QStringLiteral("请先在设置中配置 AI 服务") : error);
        return;
    }
    if (!replaceCached) {
        const auto cached = cachedRecord(question, &error);
        if (!error.isEmpty()) {
            emit analysisFailed(question.id, QStringLiteral("读取本地 AI 解析失败：%1").arg(error));
            return;
        }
        if (cached.has_value() && !isStale(*cached, question)) {
            emit analysisFinished(*cached);
            return;
        }
    }

    pendingQuestion_ = question;
    pendingConfiguration_ = configuration;
    QNetworkRequest request(AiConfigService::chatEndpoint(configuration));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(60000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + configuration.apiKey.toUtf8());
    request.setRawHeader("User-Agent", QByteArrayLiteral("QuizApp-Native"));
    const QByteArray body = QJsonDocument(requestBody(
        question, configuration, imageDataUrls(question, configuration)))
        .toJson(QJsonDocument::Compact);
    reply_ = network_->post(request, body);
    connect(reply_, &QNetworkReply::finished,
            this, &AiQuestionAnalysisService::finishReply);
    emit analysisStarted(question.id);
}

void AiQuestionAnalysisService::cancel()
{
    if (!reply_) return;
    QNetworkReply *cancelled = reply_;
    reply_ = nullptr;
    const QUuid questionId = pendingQuestion_.id;
    cancelled->disconnect(this);
    cancelled->abort();
    cancelled->deleteLater();
    emit analysisCancelled(questionId);
}

QStringList AiQuestionAnalysisService::imageDataUrls(
    const domain::Question &question,
    const AiConfiguration &configuration) const
{
    if (!configuration.visionEnabled || dataRoot_.isEmpty()) return {};
    QStringList urls;
    qint64 totalBytes = 0;
    QMimeDatabase mimeDatabase;
    for (const QString &blobId : question.questionImageBlobIds) {
        if (urls.size() >= 4) break;
        const QString relativePath = question.blobRelativePaths.value(blobId);
        if (relativePath.isEmpty() || QDir::isAbsolutePath(relativePath)
            || QDir::cleanPath(relativePath).startsWith(QStringLiteral("../"))) {
            continue;
        }
        QFile file(QDir(dataRoot_).filePath(relativePath));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 5 * 1024 * 1024
            || totalBytes + file.size() > 8 * 1024 * 1024) {
            continue;
        }
        const QByteArray bytes = file.readAll();
        totalBytes += bytes.size();
        const QString mime = mimeDatabase.mimeTypeForFileNameAndData(relativePath, bytes).name();
        if (!mime.startsWith(QStringLiteral("image/"))) continue;
        urls.append(QStringLiteral("data:%1;base64,%2")
            .arg(mime, QString::fromLatin1(bytes.toBase64())));
    }
    return urls;
}

bool AiQuestionAnalysisService::saveRecord(
    const domain::AiRecord &record,
    QString *error) const
{
    storage::Database database(connectionName());
    if (!database.open(databasePath_, error) || !database.migrate(error)) return false;
    storage::SqliteAiRecordRepository repository(database.connection());
    return repository.upsert(record, error);
}

void AiQuestionAnalysisService::finishReply()
{
    if (!reply_) return;
    QNetworkReply *finished = reply_;
    reply_ = nullptr;
    const QUuid questionId = pendingQuestion_.id;
    const QByteArray payload = finished->readAll();
    const int status = finished->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool success = finished->error() == QNetworkReply::NoError
        && status >= 200 && status < 300;
    const QString fallback = finished->error() == QNetworkReply::NoError
        ? QStringLiteral("API 返回 HTTP %1").arg(status)
        : finished->errorString();
    finished->deleteLater();
    if (!success) {
        emit analysisFailed(questionId, responseError(payload, fallback));
        return;
    }
    QString error;
    const QString content = parseResponse(payload, &error);
    if (content.isEmpty()) {
        emit analysisFailed(questionId, error);
        return;
    }
    domain::AiRecord record;
    record.id = QUuid::createUuid();
    record.recordType = QString::fromLatin1(kRecordType);
    record.sourceId = questionId.toString(QUuid::WithoutBraces);
    record.model = pendingConfiguration_.model;
    record.content = content;
    record.sourceHash = pendingQuestion_.contentHash;
    record.createdAt = QDateTime::currentDateTimeUtc();
    if (!saveRecord(record, &error)) {
        emit analysisFailed(questionId, QStringLiteral("保存 AI 解析失败：%1").arg(error));
        return;
    }
    emit analysisFinished(record);
}

} // namespace quizapp::services
