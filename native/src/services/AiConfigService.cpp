#include "services/AiConfigService.h"

#include "platform/SecureSecretStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSettings>
#include <QSet>

#include <algorithm>

namespace quizapp::services {
namespace {

void clearError(QString *error)
{
    if (error) error->clear();
}

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

QString endpoint(const QString &baseUrl, const QString &suffix)
{
    QString base = baseUrl.trimmed();
    while (base.endsWith(u'/')) base.chop(1);
    if (base.endsWith(QStringLiteral("/chat/completions"), Qt::CaseInsensitive)) {
        base.chop(QStringLiteral("/chat/completions").size());
    }
    return base + u'/' + suffix;
}

} // namespace

AiConfiguration AiConfigService::load(QSettings &settings, QString *error)
{
    clearError(error);
    AiConfiguration configuration;
    configuration.provider = settings.value(
        QStringLiteral("ai/provider"), configuration.provider).toString();
    configuration.baseUrl = settings.value(
        QStringLiteral("ai/baseUrl"), configuration.baseUrl).toString();
    configuration.model = settings.value(
        QStringLiteral("ai/model"), configuration.model).toString();
    configuration.modelOptions = settings.value(
        QStringLiteral("ai/modelOptions")).toStringList();
    configuration.maxTokens = settings.value(
        QStringLiteral("ai/maxTokens"), configuration.maxTokens).toInt();
    configuration.historyMessages = settings.value(
        QStringLiteral("ai/historyMessages"), configuration.historyMessages).toInt();
    configuration.temperature = settings.value(
        QStringLiteral("ai/temperature"), configuration.temperature).toDouble();
    configuration.visionEnabled = settings.value(
        QStringLiteral("ai/visionEnabled"), configuration.visionEnabled).toBool();
    configuration.persistThreads = settings.value(
        QStringLiteral("ai/persistThreads"), configuration.persistThreads).toBool();
    configuration.customSystemPrompt = settings.value(
        QStringLiteral("ai/customSystemPrompt")).toString();

    QString secretError;
    configuration.apiKey = platform::SecureSecretStore::readSecret(
        QStringLiteral("ai/apiKey"), settings, &secretError);
    if (!secretError.isEmpty()) {
        fail(error, secretError);
    }
    const QString legacyApiKey = settings.value(QStringLiteral("ai/apiKey")).toString().trimmed();
    if (configuration.apiKey.isEmpty() && !legacyApiKey.isEmpty()) {
        if (platform::SecureSecretStore::writeSecret(
                QStringLiteral("ai/apiKey"), legacyApiKey, settings, &secretError)) {
            configuration.apiKey = legacyApiKey;
            settings.remove(QStringLiteral("ai/apiKey"));
            settings.sync();
        } else {
            fail(error, secretError);
        }
    }
    return normalized(configuration);
}

bool AiConfigService::save(
    const AiConfiguration &configuration, QSettings &settings, QString *error)
{
    clearError(error);
    const AiConfiguration value = normalized(configuration);
    settings.setValue(QStringLiteral("ai/provider"), value.provider);
    settings.setValue(QStringLiteral("ai/baseUrl"), value.baseUrl);
    settings.setValue(QStringLiteral("ai/model"), value.model);
    settings.setValue(QStringLiteral("ai/modelOptions"), value.modelOptions);
    settings.setValue(QStringLiteral("ai/maxTokens"), value.maxTokens);
    settings.setValue(QStringLiteral("ai/historyMessages"), value.historyMessages);
    settings.setValue(QStringLiteral("ai/temperature"), value.temperature);
    settings.setValue(QStringLiteral("ai/visionEnabled"), value.visionEnabled);
    settings.setValue(QStringLiteral("ai/persistThreads"), value.persistThreads);
    settings.setValue(QStringLiteral("ai/customSystemPrompt"), value.customSystemPrompt);
    settings.remove(QStringLiteral("ai/apiKey"));
    if (!platform::SecureSecretStore::writeSecret(
            QStringLiteral("ai/apiKey"), value.apiKey, settings, error)) {
        return false;
    }
    settings.sync();
    return settings.status() == QSettings::NoError
        || fail(error, QStringLiteral("AI 设置保存失败"));
}

AiConfiguration AiConfigService::normalized(const AiConfiguration &configuration)
{
    AiConfiguration value = configuration;
    value.provider = value.provider == QStringLiteral("deepseek")
        ? value.provider : QStringLiteral("custom");
    value.baseUrl = value.baseUrl.trimmed();
    while (value.baseUrl.endsWith(u'/')) value.baseUrl.chop(1);
    if (value.provider == QStringLiteral("deepseek") && value.baseUrl.isEmpty()) {
        value.baseUrl = QStringLiteral("https://api.deepseek.com");
    }
    value.apiKey = value.apiKey.trimmed();
    value.model = value.model.trimmed();
    if (value.provider == QStringLiteral("deepseek") && value.model.isEmpty()) {
        value.model = QStringLiteral("deepseek-chat");
    }
    QSet<QString> unique;
    QStringList models;
    for (const QString &model : std::as_const(value.modelOptions)) {
        const QString clean = model.trimmed();
        if (!clean.isEmpty() && !unique.contains(clean) && models.size() < 200) {
            unique.insert(clean);
            models.append(clean);
        }
    }
    models.sort(Qt::CaseInsensitive);
    value.modelOptions = models;
    value.maxTokens = std::clamp(value.maxTokens, 128, 16384);
    value.historyMessages = std::clamp(value.historyMessages, 0, 60);
    value.temperature = std::clamp(value.temperature, 0.0, 2.0);
    value.customSystemPrompt = value.customSystemPrompt.trimmed().left(6000);
    return value;
}

bool AiConfigService::validate(
    const AiConfiguration &configuration, bool requireModel, QString *error)
{
    clearError(error);
    const AiConfiguration value = normalized(configuration);
    const QUrl url(value.baseUrl);
    if (value.baseUrl.isEmpty() || !url.isValid() || url.host().isEmpty()) {
        return fail(error, QStringLiteral("请填写有效的 API 地址"));
    }
    const bool localHttp = url.scheme() == QStringLiteral("http")
        && (url.host().compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0
            || url.host() == QStringLiteral("127.0.0.1"));
    if (url.scheme() != QStringLiteral("https") && !localHttp) {
        return fail(error, QStringLiteral("API 地址必须使用 HTTPS；本机调试可使用 localhost"));
    }
    if (value.apiKey.isEmpty()) return fail(error, QStringLiteral("请填写 API Key"));
    if (requireModel && value.model.isEmpty()) {
        return fail(error, QStringLiteral("请选择或填写模型"));
    }
    return true;
}

QUrl AiConfigService::modelsEndpoint(const AiConfiguration &configuration)
{
    return QUrl(endpoint(normalized(configuration).baseUrl, QStringLiteral("models")));
}

QUrl AiConfigService::chatEndpoint(const AiConfiguration &configuration)
{
    return QUrl(endpoint(normalized(configuration).baseUrl, QStringLiteral("chat/completions")));
}

QStringList AiConfigService::parseModelList(const QByteArray &payload, QString *error)
{
    clearError(error);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(error, QStringLiteral("模型列表不是有效 JSON"));
        return {};
    }
    QJsonArray entries;
    if (document.isObject()) entries = document.object().value(QStringLiteral("data")).toArray();
    else if (document.isArray()) entries = document.array();
    QSet<QString> unique;
    QStringList models;
    for (const QJsonValue &entry : entries) {
        QString model;
        if (entry.isString()) model = entry.toString();
        else if (entry.isObject()) {
            const QJsonObject object = entry.toObject();
            model = object.value(QStringLiteral("id")).toString();
            if (model.isEmpty()) model = object.value(QStringLiteral("name")).toString();
        }
        model = model.trimmed();
        if (!model.isEmpty() && !unique.contains(model) && models.size() < 200) {
            unique.insert(model);
            models.append(model);
        }
    }
    models.sort(Qt::CaseInsensitive);
    if (models.isEmpty()) fail(error, QStringLiteral("接口没有返回可用模型"));
    return models;
}

} // namespace quizapp::services
