#include "ui/ReviewPage.h"

#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <utility>

namespace quizapp::ui {
namespace {

void clearLayoutItems(QLayout *layout)
{
    while (layout && layout->count() > 0) {
        delete layout->takeAt(0);
    }
}

} // namespace

ReviewPage::ReviewPage(QString dataRoot, QWidget *parent)
    : QWidget(parent)
    , dataRoot_(std::move(dataRoot))
{
    setObjectName(QStringLiteral("reviewPage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *topBar = new QFrame(this);
    topBar->setObjectName(QStringLiteral("reviewTopBar"));
    topLayout_ = new QGridLayout(topBar);
    topLayout_->setContentsMargins(14, 10, 14, 10);
    topLayout_->setHorizontalSpacing(8);
    topLayout_->setVerticalSpacing(8);
    backButton_ = new QPushButton(QStringLiteral("返回学习中心"), topBar);
    backButton_->setObjectName(QStringLiteral("reviewBackButton"));
    backButton_->setProperty("quizappIcon", QStringLiteral("arrow_back"));
    backButton_->setMinimumHeight(42);
    connect(backButton_, &QPushButton::clicked, this, &ReviewPage::backRequested);
    titleLabel_ = new QLabel(QStringLiteral("到期复习"), topBar);
    titleLabel_->setObjectName(QStringLiteral("reviewTitle"));
    progressLabel_ = new QLabel(topBar);
    progressLabel_->setObjectName(QStringLiteral("reviewProgress"));
    root->addWidget(topBar);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("reviewScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 18, 18, 22);
    layout->setSpacing(14);

    pathLabel_ = new QLabel(content);
    pathLabel_->setObjectName(QStringLiteral("reviewPath"));
    pathLabel_->setWordWrap(true);
    layout->addWidget(pathLabel_);
    promptLabel_ = new QLabel(content);
    promptLabel_->setObjectName(QStringLiteral("reviewPrompt"));
    promptLabel_->setWordWrap(true);
    promptLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(promptLabel_);

    questionImagesLayout_ = new QVBoxLayout;
    questionImagesLayout_->setSpacing(10);
    layout->addLayout(questionImagesLayout_);

    auto *optionsSurface = new QFrame(content);
    optionsSurface->setObjectName(QStringLiteral("reviewOptionsSurface"));
    optionsLayout_ = new QVBoxLayout(optionsSurface);
    optionsLayout_->setContentsMargins(12, 12, 12, 12);
    optionsLayout_->setSpacing(8);
    layout->addWidget(optionsSurface);

    answerSurface_ = new QFrame(content);
    answerSurface_->setObjectName(QStringLiteral("reviewAnswerSurface"));
    auto *answerLayout = new QVBoxLayout(answerSurface_);
    answerLayout->setContentsMargins(14, 12, 14, 12);
    answerLayout->setSpacing(8);
    answerLabel_ = new QLabel(answerSurface_);
    answerLabel_->setObjectName(QStringLiteral("reviewAnswer"));
    answerLabel_->setWordWrap(true);
    explanationLabel_ = new QLabel(answerSurface_);
    explanationLabel_->setObjectName(QStringLiteral("reviewExplanation"));
    explanationLabel_->setWordWrap(true);
    explanationLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    explanationImagesLayout_ = new QVBoxLayout;
    explanationImagesLayout_->setSpacing(10);
    answerLayout->addWidget(answerLabel_);
    answerLayout->addWidget(explanationLabel_);
    answerLayout->addLayout(explanationImagesLayout_);
    layout->addWidget(answerSurface_);

    auto *secondaryActions = new QHBoxLayout;
    removeButton_ = new QPushButton(QStringLiteral("移出复习"), content);
    removeButton_->setObjectName(QStringLiteral("reviewRemoveButton"));
    handwritingButton_ = new QPushButton(QStringLiteral("手写"), content);
    handwritingButton_->setObjectName(QStringLiteral("reviewHandwritingButton"));
    handwritingButton_->setProperty("quizappIcon", QStringLiteral("stylus"));
    connect(removeButton_, &QPushButton::clicked, this, [this] {
        if (!question_.id.isNull()) {
            emit removeRequested(question_.id);
        }
    });
    connect(handwritingButton_, &QPushButton::clicked, this, [this] {
        emit handwritingRequested(notebookContext());
    });
    secondaryActions->addWidget(removeButton_);
    secondaryActions->addWidget(handwritingButton_);
    secondaryActions->addStretch();
    layout->addLayout(secondaryActions);
    layout->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto *actions = new QFrame(this);
    actions->setObjectName(QStringLiteral("reviewActions"));
    actionsLayout_ = new QVBoxLayout(actions);
    actionsLayout_->setContentsMargins(12, 9, 12, 9);
    actionsLayout_->setSpacing(8);
    revealButton_ = new QPushButton(QStringLiteral("显示答案"), actions);
    revealButton_->setObjectName(QStringLiteral("reviewRevealButton"));
    revealButton_->setMinimumHeight(44);
    connect(revealButton_, &QPushButton::clicked, this, [this] {
        revealed_ = true;
        render();
    });
    actionsLayout_->addWidget(revealButton_);
    auto *ratings = new QHBoxLayout;
    ratings->setSpacing(7);
    const std::array<QString, 4> labels{
        QStringLiteral("忘记"), QStringLiteral("困难"),
        QStringLiteral("良好"), QStringLiteral("简单")};
    for (int index = 0; index < 4; ++index) {
        auto *button = new QPushButton(labels.at(static_cast<size_t>(index)), actions);
        button->setObjectName(QStringLiteral("reviewRating%1").arg(index + 1));
        button->setProperty("reviewRating", index + 1);
        button->setMinimumHeight(48);
        connect(button, &QPushButton::clicked, this, [this, index] {
            if (revealed_ && !question_.id.isNull()) {
                emit ratingRequested(
                    question_.id,
                    static_cast<domain::ReviewRating>(index + 1));
            }
        });
        ratingButtons_.at(static_cast<size_t>(index)) = button;
        ratings->addWidget(button, 1);
    }
    actionsLayout_->addLayout(ratings);
    root->addWidget(actions);
    updateResponsiveHeader();
    showEmpty();
}

void ReviewPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResponsiveHeader();
}

void ReviewPage::updateResponsiveHeader()
{
    const bool compact = width() < 620;
    clearLayoutItems(topLayout_);
    backButton_->setText(compact ? QStringLiteral("返回") : QStringLiteral("返回学习中心"));
    if (compact) {
        topLayout_->addWidget(backButton_, 0, 0);
        topLayout_->addWidget(titleLabel_, 0, 1);
        topLayout_->addWidget(progressLabel_, 1, 0, 1, 2, Qt::AlignRight);
        topLayout_->setColumnStretch(0, 0);
        topLayout_->setColumnStretch(1, 1);
        topLayout_->setColumnStretch(2, 0);
    } else {
        topLayout_->addWidget(backButton_, 0, 0);
        topLayout_->addWidget(titleLabel_, 0, 1);
        topLayout_->addWidget(progressLabel_, 0, 2);
        topLayout_->setColumnStretch(0, 0);
        topLayout_->setColumnStretch(1, 1);
        topLayout_->setColumnStretch(2, 0);
    }
}

void ReviewPage::showCard(
    const domain::Question &question,
    const domain::ReviewCard &card,
    const std::array<domain::ReviewPreview, 4> &previews,
    int remainingCount,
    const QUuid &sessionId)
{
    question_ = question;
    card_ = card;
    previews_ = previews;
    remainingCount_ = remainingCount;
    sessionId_ = sessionId;
    revealed_ = false;
    render();
}

void ReviewPage::showEmpty()
{
    question_ = {};
    card_ = {};
    remainingCount_ = 0;
    revealed_ = false;
    render();
}

std::optional<QUuid> ReviewPage::currentQuestionId() const
{
    return question_.id.isNull() ? std::nullopt : std::optional<QUuid>(question_.id);
}

domain::NotebookLaunchContext ReviewPage::notebookContext() const
{
    domain::NotebookLaunchContext context;
    context.sessionId = sessionId_;
    context.questionId = question_.id;
    context.practiceMode = domain::PracticeMode::Review;
    context.practiceViewport.insert(QStringLiteral("remaining"), remainingCount_);
    return context;
}

void ReviewPage::render()
{
    const bool active = !question_.id.isNull();
    const bool questionChanged = renderedQuestionId_ != question_.id;
    progressLabel_->setText(active
        ? QStringLiteral("剩余 %1").arg(remainingCount_) : QString());
    pathLabel_->setText(active
        ? question_.path.join(QStringLiteral(" / ")) : QStringLiteral("复习完成"));
    promptLabel_->setText(active
        ? question_.prompt : QStringLiteral("当前没有到期题目。"));
    if (questionChanged) {
        clearLayout(questionImagesLayout_);
        clearLayout(optionsLayout_);
        clearLayout(explanationImagesLayout_);
    }
    if (active && questionChanged) {
        addImages(
            questionImagesLayout_, question_.questionImageBlobIds,
            QStringLiteral("reviewQuestionImage"));
        QStringList options = question_.options;
        if (question_.type == domain::QuestionType::Boolean && options.isEmpty()) {
            options = {QStringLiteral("正确"), QStringLiteral("错误")};
        }
        for (qsizetype index = 0; index < options.size(); ++index) {
            auto *option = new QLabel(
                QStringLiteral("%1. %2")
                    .arg(QChar(static_cast<char16_t>(u'A' + index)), options.at(index)),
                this);
            option->setObjectName(QStringLiteral("reviewOption"));
            option->setWordWrap(true);
            optionsLayout_->addWidget(option);
        }
    }
    renderedQuestionId_ = question_.id;
    answerSurface_->setVisible(active && revealed_);
    if (active && revealed_) {
        answerLabel_->setText(question_.correctAnswer.trimmed().isEmpty()
            ? QStringLiteral("正确答案：见内置解析")
            : QStringLiteral("正确答案：%1").arg(question_.correctAnswer));
        explanationLabel_->setText(question_.builtinExplanation.text.trimmed().isEmpty()
            ? QStringLiteral("暂无内置解析") : question_.builtinExplanation.text);
        if (questionChanged || explanationImagesLayout_->count() == 0) {
            clearLayout(explanationImagesLayout_);
            addImages(
                explanationImagesLayout_, question_.builtinExplanation.imageBlobIds,
                QStringLiteral("reviewExplanationImage"));
        }
    }
    const bool showReveal = active && !revealed_;
    if (showReveal && actionsLayout_->indexOf(revealButton_) < 0) {
        actionsLayout_->insertWidget(0, revealButton_);
    } else if (!showReveal && actionsLayout_->indexOf(revealButton_) >= 0) {
        actionsLayout_->removeWidget(revealButton_);
    }
    revealButton_->setMinimumHeight(showReveal ? 44 : 0);
    revealButton_->setMaximumHeight(showReveal ? QWIDGETSIZE_MAX : 0);
    revealButton_->setVisible(showReveal);
    actionsLayout_->invalidate();
    actionsLayout_->activate();
    removeButton_->setEnabled(active);
    handwritingButton_->setEnabled(active);
    const std::array<QString, 4> labels{
        QStringLiteral("忘记"), QStringLiteral("困难"),
        QStringLiteral("良好"), QStringLiteral("简单")};
    for (int index = 0; index < 4; ++index) {
        QPushButton *button = ratingButtons_.at(static_cast<size_t>(index));
        button->setEnabled(active && revealed_);
        button->setText(active
            ? QStringLiteral("%1 %2")
                  .arg(labels.at(static_cast<size_t>(index)),
                       intervalText(previews_.at(static_cast<size_t>(index)).scheduledDays))
            : labels.at(static_cast<size_t>(index)));
    }
}

void ReviewPage::clearLayout(QVBoxLayout *layout)
{
    while (layout && layout->count() > 0) {
        QLayoutItem *item = layout->takeAt(0);
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void ReviewPage::addImages(
    QVBoxLayout *layout,
    const QStringList &blobIds,
    const QString &objectName)
{
    if (!layout || dataRoot_.isEmpty()) {
        return;
    }
    const int maximumWidth = qBound(280, width() - 64, 860);
    for (const QString &blobId : blobIds) {
        const QString relativePath = question_.blobRelativePaths.value(blobId);
        const QPixmap pixmap(QDir(dataRoot_).filePath(relativePath));
        if (relativePath.isEmpty() || pixmap.isNull()) {
            continue;
        }
        auto *label = new QLabel(this);
        label->setObjectName(objectName);
        label->setAlignment(Qt::AlignCenter);
        const int targetWidth = qMin(maximumWidth, pixmap.width());
        label->setPixmap(targetWidth < pixmap.width()
            ? pixmap.scaledToWidth(targetWidth, Qt::SmoothTransformation) : pixmap);
        layout->addWidget(label);
    }
}

QString ReviewPage::intervalText(int days) const
{
    if (days < 30) {
        return QStringLiteral("%1天").arg(days);
    }
    if (days < 365) {
        return QStringLiteral("约%1个月").arg(qMax(1, qRound(days / 30.0)));
    }
    return QStringLiteral("约%1年").arg(qMax(1, qRound(days / 365.0)));
}

} // namespace quizapp::ui
