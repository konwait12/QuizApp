#include "ui/AiSettingsPanel.h"

#include "ui/ChoiceComboBox.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace quizapp::ui {

AiSettingsPanel::AiSettingsPanel(QWidget *parent)
    : QFrame(parent)
    , network_(new QNetworkAccessManager(this))
{
    setObjectName(QStringLiteral("aiSettingsSurface"));
    setMaximumWidth(760);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("AI 服务与模型"), this);
    title->setObjectName(QStringLiteral("settingsSectionHeading"));
    auto *description = new QLabel(
        QStringLiteral("API Key 使用系统安全存储，只在请求所选服务时读取。修改后请点击设置页的“保存设置”。"),
        this);
    description->setObjectName(QStringLiteral("pageSupportingText"));
    description->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(description);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    providerChoice_ = new ChoiceComboBox(this);
    providerChoice_->setObjectName(QStringLiteral("aiProviderChoice"));
    providerChoice_->addItem(QStringLiteral("DeepSeek"), QStringLiteral("deepseek"));
    providerChoice_->addItem(QStringLiteral("OpenAI 兼容 API"), QStringLiteral("custom"));
    providerChoice_->setMinimumHeight(42);
    form->addRow(QStringLiteral("服务预设"), providerChoice_);

    baseUrlInput_ = new QLineEdit(this);
    baseUrlInput_->setObjectName(QStringLiteral("aiBaseUrlInput"));
    baseUrlInput_->setPlaceholderText(QStringLiteral("https://api.example.com/v1"));
    baseUrlInput_->setMinimumHeight(42);
    form->addRow(QStringLiteral("API 地址"), baseUrlInput_);

    apiKeyInput_ = new QLineEdit(this);
    apiKeyInput_->setObjectName(QStringLiteral("aiApiKeyInput"));
    apiKeyInput_->setEchoMode(QLineEdit::Password);
    apiKeyInput_->setPlaceholderText(QStringLiteral("sk-..."));
    apiKeyInput_->setMinimumHeight(42);
    showApiKeyChoice_ = new QCheckBox(QStringLiteral("显示"), this);
    connect(showApiKeyChoice_, &QCheckBox::toggled, this, [this](bool visible) {
        apiKeyInput_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
    });
    auto *keyRow = new QHBoxLayout;
    keyRow->setContentsMargins(0, 0, 0, 0);
    keyRow->setSpacing(10);
    keyRow->addWidget(apiKeyInput_, 1);
    keyRow->addWidget(showApiKeyChoice_);
    form->addRow(QStringLiteral("API Key"), keyRow);

    modelChoice_ = new ChoiceComboBox(this);
    modelChoice_->setObjectName(QStringLiteral("aiModelChoice"));
    modelChoice_->setEditable(true);
    modelChoice_->setInsertPolicy(QComboBox::NoInsert);
    modelChoice_->setMinimumHeight(42);
    connect(modelChoice_->lineEdit(), &QLineEdit::editingFinished,
            this, [this] { modelChoice_->lineEdit()->setCursorPosition(0); });
    connect(modelChoice_, &QComboBox::currentTextChanged, this, [this] {
        modelChoice_->setToolTip(modelChoice_->currentText());
        if (!modelChoice_->lineEdit()->hasFocus()) {
            modelChoice_->lineEdit()->setCursorPosition(0);
        }
    });
    refreshModelsButton_ = new QPushButton(this);
    refreshModelsButton_->setObjectName(QStringLiteral("aiRefreshModelsButton"));
    refreshModelsButton_->setText(QStringLiteral("↻"));
    QFont refreshFont = refreshModelsButton_->font();
    refreshFont.setPointSize(16);
    refreshModelsButton_->setFont(refreshFont);
    refreshModelsButton_->setToolTip(QStringLiteral("读取模型列表"));
    refreshModelsButton_->setAccessibleName(QStringLiteral("读取模型列表"));
    refreshModelsButton_->setFixedSize(44, 42);
    auto *modelRow = new QHBoxLayout;
    modelRow->setContentsMargins(0, 0, 0, 0);
    modelRow->setSpacing(8);
    modelRow->addWidget(modelChoice_, 1);
    modelRow->addWidget(refreshModelsButton_);
    form->addRow(QStringLiteral("模型"), modelRow);
    layout->addLayout(form);

    auto *advancedTitle = new QLabel(QStringLiteral("生成与上下文"), this);
    advancedTitle->setObjectName(QStringLiteral("settingsFieldLabel"));
    layout->addWidget(advancedTitle);
    auto *advanced = new QGridLayout;
    advanced->setHorizontalSpacing(12);
    advanced->setVerticalSpacing(10);
    maxTokensInput_ = new QSpinBox(this);
    maxTokensInput_->setRange(128, 16384);
    maxTokensInput_->setSingleStep(128);
    maxTokensInput_->setMinimumHeight(38);
    historyMessagesInput_ = new QSpinBox(this);
    historyMessagesInput_->setRange(0, 60);
    historyMessagesInput_->setMinimumHeight(38);
    temperatureInput_ = new QDoubleSpinBox(this);
    temperatureInput_->setRange(0.0, 2.0);
    temperatureInput_->setSingleStep(0.1);
    temperatureInput_->setDecimals(1);
    temperatureInput_->setMinimumHeight(38);
    advanced->addWidget(new QLabel(QStringLiteral("最大输出 tokens"), this), 0, 0);
    advanced->addWidget(maxTokensInput_, 0, 1);
    advanced->addWidget(new QLabel(QStringLiteral("读取历史消息数"), this), 1, 0);
    advanced->addWidget(historyMessagesInput_, 1, 1);
    advanced->addWidget(new QLabel(QStringLiteral("温度"), this), 2, 0);
    advanced->addWidget(temperatureInput_, 2, 1);
    advanced->setColumnStretch(1, 1);
    layout->addLayout(advanced);

    visionChoice_ = new QCheckBox(QStringLiteral("当前模型支持图片输入"), this);
    persistThreadsChoice_ = new QCheckBox(QStringLiteral("在本机保存 AI 对话"), this);
    layout->addWidget(visionChoice_);
    layout->addWidget(persistThreadsChoice_);
    auto *promptLabel = new QLabel(QStringLiteral("追加系统提示词"), this);
    promptLabel->setObjectName(QStringLiteral("settingsFieldLabel"));
    customPromptInput_ = new QTextEdit(this);
    customPromptInput_->setObjectName(QStringLiteral("aiCustomPromptInput"));
    customPromptInput_->setPlaceholderText(QStringLiteral("可选，最多 6000 字符"));
    customPromptInput_->setMaximumHeight(110);
    layout->addWidget(promptLabel);
    layout->addWidget(customPromptInput_);

    testConnectionButton_ = new QPushButton(QStringLiteral("测试连接"), this);
    testConnectionButton_->setObjectName(QStringLiteral("aiTestConnectionButton"));
    testConnectionButton_->setMinimumHeight(42);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("aiRequestProgress"));
    progress_->setRange(0, 0);
    progress_->hide();
    status_ = new QLabel(QStringLiteral("尚未测试连接"), this);
    status_->setObjectName(QStringLiteral("aiSettingsStatus"));
    status_->setWordWrap(true);
    auto *actions = new QHBoxLayout;
    actions->addWidget(status_, 1);
    actions->addWidget(testConnectionButton_);
    layout->addWidget(progress_);
    layout->addLayout(actions);

    connect(providerChoice_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] { applyProviderPreset(); });
    connect(refreshModelsButton_, &QPushButton::clicked,
            this, &AiSettingsPanel::refreshModels);
    connect(testConnectionButton_, &QPushButton::clicked,
            this, &AiSettingsPanel::testConnection);

    QSettings settings;
    QString error;
    populate(services::AiConfigService::load(settings, &error));
    if (!error.isEmpty()) status_->setText(error);
}

services::AiConfiguration AiSettingsPanel::configuration() const
{
    services::AiConfiguration value;
    value.provider = providerChoice_->currentData().toString();
    value.baseUrl = baseUrlInput_->text();
    value.apiKey = apiKeyInput_->text();
    value.model = modelChoice_->currentText();
    value.modelOptions = modelOptions_;
    value.maxTokens = maxTokensInput_->value();
    value.historyMessages = historyMessagesInput_->value();
    value.temperature = temperatureInput_->value();
    value.visionEnabled = visionChoice_->isChecked();
    value.persistThreads = persistThreadsChoice_->isChecked();
    value.customSystemPrompt = customPromptInput_->toPlainText();
    return services::AiConfigService::normalized(value);
}

bool AiSettingsPanel::save(QString *error)
{
    QSettings settings;
    if (!services::AiConfigService::save(configuration(), settings, error)) return false;
    status_->setText(QStringLiteral("AI 设置已安全保存"));
    emit configurationSaved();
    return true;
}

void AiSettingsPanel::populate(const services::AiConfiguration &configuration)
{
    populating_ = true;
    const int providerIndex = providerChoice_->findData(configuration.provider);
    providerChoice_->setCurrentIndex(providerIndex >= 0 ? providerIndex : 0);
    baseUrlInput_->setText(configuration.baseUrl);
    apiKeyInput_->setText(configuration.apiKey);
    modelOptions_ = configuration.modelOptions;
    modelChoice_->clear();
    modelChoice_->addItems(modelOptions_);
    modelChoice_->setCurrentText(configuration.model);
    modelChoice_->setToolTip(configuration.model);
    modelChoice_->lineEdit()->setCursorPosition(0);
    maxTokensInput_->setValue(configuration.maxTokens);
    historyMessagesInput_->setValue(configuration.historyMessages);
    temperatureInput_->setValue(configuration.temperature);
    visionChoice_->setChecked(configuration.visionEnabled);
    persistThreadsChoice_->setChecked(configuration.persistThreads);
    customPromptInput_->setPlainText(configuration.customSystemPrompt);
    baseUrlInput_->setReadOnly(configuration.provider == QStringLiteral("deepseek"));
    populating_ = false;
}

void AiSettingsPanel::applyProviderPreset()
{
    if (populating_) return;
    const bool deepseek = providerChoice_->currentData().toString()
        == QStringLiteral("deepseek");
    baseUrlInput_->setReadOnly(deepseek);
    if (deepseek) {
        baseUrlInput_->setText(QStringLiteral("https://api.deepseek.com"));
        modelChoice_->setCurrentText(QStringLiteral("deepseek-chat"));
    }
}

void AiSettingsPanel::refreshModels()
{
    const services::AiConfiguration value = configuration();
    QString error;
    if (!services::AiConfigService::validate(value, false, &error)) {
        status_->setText(error);
        return;
    }
    QNetworkRequest request(services::AiConfigService::modelsEndpoint(value));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + value.apiKey.toUtf8());
    request.setRawHeader("User-Agent", QByteArrayLiteral("QuizApp-Native"));
    startRequest(RequestKind::Models, network_->get(request));
}

void AiSettingsPanel::testConnection()
{
    const services::AiConfiguration value = configuration();
    QString error;
    if (!services::AiConfigService::validate(value, true, &error)) {
        status_->setText(error);
        return;
    }
    QNetworkRequest request(services::AiConfigService::chatEndpoint(value));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + value.apiKey.toUtf8());
    request.setRawHeader("User-Agent", QByteArrayLiteral("QuizApp-Native"));
    const QJsonObject body{
        {QStringLiteral("model"), value.model},
        {QStringLiteral("messages"), QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                        {QStringLiteral("content"), QStringLiteral("只回复“连接成功”。")}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), QStringLiteral("测试连接")}},
        }},
        {QStringLiteral("max_tokens"), 32},
        {QStringLiteral("temperature"), 0.0},
        {QStringLiteral("stream"), false},
    };
    startRequest(RequestKind::ConnectionTest,
                 network_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)));
}

void AiSettingsPanel::startRequest(RequestKind kind, QNetworkReply *reply)
{
    if (reply_) {
        reply_->abort();
        reply_->deleteLater();
    }
    requestKind_ = kind;
    reply_ = reply;
    setBusy(true, kind == RequestKind::Models
        ? QStringLiteral("正在读取模型列表") : QStringLiteral("正在测试连接"));
    connect(reply_, &QNetworkReply::finished, this, &AiSettingsPanel::finishRequest);
}

void AiSettingsPanel::finishRequest()
{
    if (!reply_) return;
    QNetworkReply *finished = reply_;
    reply_ = nullptr;
    const RequestKind kind = requestKind_;
    requestKind_ = RequestKind::None;
    const QByteArray payload = finished->readAll();
    const QString fallback = finished->error() == QNetworkReply::NoError
        ? QStringLiteral("API 返回 %1").arg(finished->attribute(
              QNetworkRequest::HttpStatusCodeAttribute).toInt())
        : finished->errorString();
    const bool success = finished->error() == QNetworkReply::NoError
        && finished->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 200
        && finished->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() < 300;
    finished->deleteLater();
    setBusy(false);
    if (!success) {
        status_->setText(QStringLiteral("请求失败：%1").arg(responseError(payload, fallback)));
        return;
    }
    if (kind == RequestKind::Models) {
        QString error;
        const QStringList models = services::AiConfigService::parseModelList(payload, &error);
        if (models.isEmpty()) {
            status_->setText(error);
            return;
        }
        const QString current = modelChoice_->currentText();
        modelOptions_ = models;
        modelChoice_->clear();
        modelChoice_->addItems(models);
        modelChoice_->setCurrentText(current.isEmpty() ? models.first() : current);
        modelChoice_->lineEdit()->setCursorPosition(0);
        status_->setText(QStringLiteral("已读取 %1 个模型").arg(models.size()));
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    QString content;
    if (document.isObject()) {
        const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            content = choices.first().toObject().value(QStringLiteral("message"))
                          .toObject().value(QStringLiteral("content")).toString().trimmed();
        }
    }
    status_->setText(content.isEmpty()
        ? QStringLiteral("连接成功")
        : QStringLiteral("连接成功：%1").arg(content.left(40)));
}

void AiSettingsPanel::setBusy(bool busy, const QString &status)
{
    refreshModelsButton_->setEnabled(!busy);
    testConnectionButton_->setEnabled(!busy);
    progress_->setVisible(busy);
    if (!status.isEmpty()) status_->setText(status);
}

QString AiSettingsPanel::responseError(
    const QByteArray &payload, const QString &fallback) const
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (document.isObject()) {
        const QJsonObject object = document.object();
        QString message = object.value(QStringLiteral("error")).toObject()
                              .value(QStringLiteral("message")).toString();
        if (message.isEmpty()) message = object.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) return message.left(300);
    }
    return fallback;
}

} // namespace quizapp::ui
