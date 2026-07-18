#pragma once

#include "services/AiConfigService.h"

#include <QFrame>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTextEdit;

namespace quizapp::ui {

class ChoiceComboBox;

class AiSettingsPanel final : public QFrame {
    Q_OBJECT

public:
    explicit AiSettingsPanel(QWidget *parent = nullptr);
    bool save(QString *error = nullptr);
    services::AiConfiguration configuration() const;

signals:
    void configurationSaved();

private:
    enum class RequestKind { None, Models, ConnectionTest };
    void populate(const services::AiConfiguration &configuration);
    void applyProviderPreset();
    void refreshModels();
    void testConnection();
    void startRequest(RequestKind kind, QNetworkReply *reply);
    void finishRequest();
    void setBusy(bool busy, const QString &status = {});
    QString responseError(const QByteArray &payload, const QString &fallback) const;

    ChoiceComboBox *providerChoice_ = nullptr;
    QLineEdit *baseUrlInput_ = nullptr;
    QLineEdit *apiKeyInput_ = nullptr;
    QCheckBox *showApiKeyChoice_ = nullptr;
    ChoiceComboBox *modelChoice_ = nullptr;
    QPushButton *refreshModelsButton_ = nullptr;
    QPushButton *testConnectionButton_ = nullptr;
    QSpinBox *maxTokensInput_ = nullptr;
    QSpinBox *historyMessagesInput_ = nullptr;
    QDoubleSpinBox *temperatureInput_ = nullptr;
    QCheckBox *visionChoice_ = nullptr;
    QCheckBox *persistThreadsChoice_ = nullptr;
    QTextEdit *customPromptInput_ = nullptr;
    QLabel *status_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QNetworkAccessManager *network_ = nullptr;
    QNetworkReply *reply_ = nullptr;
    RequestKind requestKind_ = RequestKind::None;
    QStringList modelOptions_;
    bool populating_ = false;
};

} // namespace quizapp::ui
