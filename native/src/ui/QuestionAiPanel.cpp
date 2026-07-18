#include "ui/QuestionAiPanel.h"

#include "services/AiQuestionAnalysisService.h"

#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace quizapp::ui {

QuestionAiPanel::QuestionAiPanel(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("questionAiSurface"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("AI 题目分析"), this);
    title->setObjectName(QStringLiteral("questionAiTitle"));
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("questionAiStatus"));
    status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(title);
    header->addWidget(status_, 1);
    layout->addLayout(header);

    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("questionAiProgress"));
    progress_->setRange(0, 0);
    progress_->hide();
    layout->addWidget(progress_);

    content_ = new QTextBrowser(this);
    content_->setObjectName(QStringLiteral("questionAiContent"));
    content_->setOpenExternalLinks(false);
    content_->setMinimumHeight(120);
    content_->setMaximumHeight(360);
    layout->addWidget(content_);

    auto *actions = new QHBoxLayout;
    actions->addStretch();
    exportButton_ = new QPushButton(QStringLiteral("导出"), this);
    exportButton_->setObjectName(QStringLiteral("questionAiExportButton"));
    cancelButton_ = new QPushButton(QStringLiteral("取消"), this);
    cancelButton_->setObjectName(QStringLiteral("questionAiCancelButton"));
    analyzeButton_ = new QPushButton(QStringLiteral("开始分析"), this);
    analyzeButton_->setObjectName(QStringLiteral("questionAiAnalyzeButton"));
    for (QPushButton *button : {exportButton_, cancelButton_, analyzeButton_}) {
        button->setMinimumHeight(40);
        actions->addWidget(button);
    }
    layout->addLayout(actions);

    connect(analyzeButton_, &QPushButton::clicked, this, [this] {
        if (!question_.id.isNull()) emit analyzeRequested(question_, record_.has_value());
    });
    connect(cancelButton_, &QPushButton::clicked,
            this, &QuestionAiPanel::cancelRequested);
    connect(exportButton_, &QPushButton::clicked,
            this, &QuestionAiPanel::exportRecord);
    refresh();
}

void QuestionAiPanel::setQuestion(
    const domain::Question &question,
    const std::optional<domain::AiRecord> &record)
{
    question_ = question;
    record_ = record;
    error_.clear();
    loading_ = false;
    refresh();
}

void QuestionAiPanel::setLoading(const QUuid &questionId)
{
    if (question_.id != questionId) return;
    loading_ = true;
    error_.clear();
    refresh();
}

void QuestionAiPanel::setRecord(const domain::AiRecord &record)
{
    if (question_.id.toString(QUuid::WithoutBraces) != record.sourceId) return;
    record_ = record;
    loading_ = false;
    error_.clear();
    refresh();
}

void QuestionAiPanel::setError(const QUuid &questionId, const QString &message)
{
    if (question_.id != questionId) return;
    loading_ = false;
    error_ = message;
    refresh();
}

void QuestionAiPanel::setCancelled(const QUuid &questionId)
{
    if (question_.id != questionId) return;
    loading_ = false;
    error_ = QStringLiteral("已取消本次分析");
    refresh();
}

void QuestionAiPanel::refresh()
{
    const bool hasRecord = record_.has_value();
    const bool stale = hasRecord
        && services::AiQuestionAnalysisService::isStale(*record_, question_);
    progress_->setVisible(loading_);
    analyzeButton_->setEnabled(!loading_ && !question_.id.isNull());
    analyzeButton_->setText(hasRecord ? QStringLiteral("重新分析") : QStringLiteral("开始分析"));
    cancelButton_->setVisible(loading_);
    exportButton_->setVisible(hasRecord && !loading_);
    if (loading_) {
        status_->setText(QStringLiteral("正在分析"));
    } else if (!error_.isEmpty()) {
        status_->setText(error_);
    } else if (stale) {
        status_->setText(QStringLiteral("题目已更新，当前为旧解析"));
    } else if (hasRecord) {
        status_->setText(QStringLiteral("%1 · %2")
            .arg(record_->model,
                 record_->createdAt.toLocalTime().toString(QStringLiteral("MM-dd HH:mm"))));
    } else {
        status_->setText(QStringLiteral("仅点击后联网"));
    }
    content_->setVisible(hasRecord || !error_.isEmpty());
    if (hasRecord) {
        content_->setMarkdown(record_->content);
    } else if (!error_.isEmpty()) {
        content_->setPlainText(error_);
    } else {
        content_->clear();
    }
}

void QuestionAiPanel::exportRecord()
{
    if (!record_.has_value()) return;
    const QString filter = QStringLiteral("Markdown (*.md);;JSON (*.json)");
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 AI 题目分析"),
        QStringLiteral("题目AI解析.md"), filter, &selectedFilter);
    if (path.isEmpty()) return;
    QByteArray bytes;
    if (selectedFilter.startsWith(QStringLiteral("JSON")) || path.endsWith(
            QStringLiteral(".json"), Qt::CaseInsensitive)) {
        if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".json");
        }
        const QJsonObject object{
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("type"), record_->recordType},
            {QStringLiteral("questionId"), record_->sourceId},
            {QStringLiteral("model"), record_->model},
            {QStringLiteral("createdAt"), record_->createdAt.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("content"), record_->content},
        };
        bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    } else {
        if (!path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".md");
        }
        const QString markdown = QStringLiteral(
            "# AI 题目分析\n\n- 模型：%1\n- 时间：%2\n- 题目 ID：%3\n\n%4\n")
            .arg(record_->model,
                 record_->createdAt.toLocalTime().toString(Qt::ISODate),
                 record_->sourceId,
                 record_->content);
        bytes = markdown.toUtf8();
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(bytes);
}

} // namespace quizapp::ui
