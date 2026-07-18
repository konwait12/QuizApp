#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

class QSettings;

namespace quizapp::services {

struct AiConfiguration {
    QString provider = QStringLiteral("deepseek");
    QString baseUrl = QStringLiteral("https://api.deepseek.com");
    QString apiKey;
    QString model = QStringLiteral("deepseek-chat");
    QStringList modelOptions;
    int maxTokens = 1800;
    int historyMessages = 12;
    double temperature = 0.3;
    bool visionEnabled = false;
    bool persistThreads = true;
    QString customSystemPrompt;
};

class AiConfigService final {
public:
    static AiConfiguration load(QSettings &settings, QString *error = nullptr);
    static bool save(
        const AiConfiguration &configuration,
        QSettings &settings,
        QString *error = nullptr);
    static AiConfiguration normalized(const AiConfiguration &configuration);
    static bool validate(
        const AiConfiguration &configuration,
        bool requireModel,
        QString *error = nullptr);
    static QUrl modelsEndpoint(const AiConfiguration &configuration);
    static QUrl chatEndpoint(const AiConfiguration &configuration);
    static QStringList parseModelList(const QByteArray &payload, QString *error = nullptr);
};

} // namespace quizapp::services
