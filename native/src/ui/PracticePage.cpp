#include "ui/PracticePage.h"
#include "ui/QuestionAiPanel.h"
#include "ui/QuestionOverviewDialog.h"

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
#include <QSet>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace quizapp::ui {
namespace {

QString joinPath(const QStringList &path)
{
    return path.join(QStringLiteral(" / "));
}

void clearLayoutItems(QLayout *layout)
{
    while (layout && layout->count() > 0) {
        delete layout->takeAt(0);
    }
}

} // namespace

PracticePage::PracticePage(QString dataRoot, QWidget *parent)
    : QWidget(parent)
    , dataRoot_(std::move(dataRoot))
{
    setObjectName(QStringLiteral("practicePage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *topBar = new QFrame(this);
    topBar->setObjectName(QStringLiteral("practiceTopBar"));
    topLayout_ = new QGridLayout(topBar);
    topLayout_->setContentsMargins(14, 10, 14, 10);
    topLayout_->setHorizontalSpacing(8);
    topLayout_->setVerticalSpacing(8);
    backButton_ = new QPushButton(QStringLiteral("返回题库"), topBar);
    backButton_->setObjectName(QStringLiteral("practiceBackButton"));
    backButton_->setProperty("quizappIcon", QStringLiteral("arrow_back"));
    backButton_->setMinimumHeight(42);
    connect(backButton_, &QPushButton::clicked, this, &PracticePage::backRequested);
    bankTitle_ = new QLabel(topBar);
    bankTitle_->setObjectName(QStringLiteral("practiceBankTitle"));
    bankTitle_->setWordWrap(true);

    headerActions_ = new QWidget(topBar);
    headerActions_->setObjectName(QStringLiteral("practiceHeaderActions"));
    auto *headerActionsLayout = new QHBoxLayout(headerActions_);
    headerActionsLayout->setContentsMargins(0, 0, 0, 0);
    headerActionsLayout->setSpacing(8);
    resetButton_ = new QToolButton(headerActions_);
    resetButton_->setObjectName(QStringLiteral("practiceResetButton"));
    resetButton_->setProperty("quizappIcon", QStringLiteral("redo"));
    resetButton_->setToolTip(QStringLiteral("重做当前练习"));
    resetButton_->setAccessibleName(QStringLiteral("重做当前练习"));
    resetButton_->setFixedSize(42, 42);
    connect(resetButton_, &QToolButton::clicked, this, &PracticePage::resetRequested);
    headerActionsLayout->addWidget(resetButton_);
    saveButton_ = new QToolButton(headerActions_);
    saveButton_->setObjectName(QStringLiteral("practiceSaveButton"));
    saveButton_->setProperty("quizappIcon", QStringLiteral("save"));
    saveButton_->setToolTip(QStringLiteral("保存当前进度"));
    saveButton_->setAccessibleName(QStringLiteral("保存当前进度"));
    saveButton_->setFixedSize(42, 42);
    connect(saveButton_, &QToolButton::clicked, this, &PracticePage::manualSaveRequested);
    headerActionsLayout->addWidget(saveButton_);
    overviewButton_ = new QToolButton(headerActions_);
    overviewButton_->setObjectName(QStringLiteral("practiceOverviewButton"));
    overviewButton_->setProperty("quizappIcon", QStringLiteral("grid_view"));
    overviewButton_->setToolTip(QStringLiteral("题号总览"));
    overviewButton_->setAccessibleName(QStringLiteral("题号总览"));
    overviewButton_->setFixedSize(42, 42);
    connect(overviewButton_, &QToolButton::clicked,
            this, &PracticePage::showQuestionOverview);
    headerActionsLayout->addWidget(overviewButton_);
    saveStatusLabel_ = new QLabel(headerActions_);
    saveStatusLabel_->setObjectName(QStringLiteral("practiceSaveStatus"));
    saveStatusLabel_->setAccessibleName(QStringLiteral("练习保存状态"));
    saveStatusLabel_->hide();
    headerActionsLayout->addWidget(saveStatusLabel_);
    progressLabel_ = new QLabel(headerActions_);
    progressLabel_->setObjectName(QStringLiteral("practiceProgressLabel"));
    headerActionsLayout->addWidget(progressLabel_);
    root->addWidget(topBar);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("practiceScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("practiceContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 18, 18, 22);
    contentLayout->setSpacing(14);

    typeLabel_ = new QLabel(content);
    typeLabel_->setObjectName(QStringLiteral("practiceQuestionType"));
    contentLayout->addWidget(typeLabel_);

    promptLabel_ = new QLabel(content);
    promptLabel_->setObjectName(QStringLiteral("practicePrompt"));
    promptLabel_->setWordWrap(true);
    promptLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    contentLayout->addWidget(promptLabel_);

    questionImagesLayout_ = new QVBoxLayout;
    questionImagesLayout_->setContentsMargins(0, 0, 0, 0);
    questionImagesLayout_->setSpacing(10);
    contentLayout->addLayout(questionImagesLayout_);

    auto *optionsSurface = new QFrame(content);
    optionsSurface->setObjectName(QStringLiteral("practiceOptionsSurface"));
    optionsLayout_ = new QVBoxLayout(optionsSurface);
    optionsLayout_->setContentsMargins(0, 0, 0, 0);
    optionsLayout_->setSpacing(8);
    contentLayout->addWidget(optionsSurface);

    wrongBookButton_ = new QPushButton(content);
    wrongBookButton_->setObjectName(QStringLiteral("practiceWrongBookButton"));
    wrongBookButton_->setMinimumHeight(44);
    wrongBookButton_->hide();
    connect(wrongBookButton_, &QPushButton::clicked, this, [this] {
        const auto questionId = currentQuestionId();
        if (!questionId.has_value()) {
            return;
        }
        emit wrongBookToggleRequested(
            *questionId, !wrongBookQuestionIds_.contains(*questionId));
    });
    contentLayout->addWidget(wrongBookButton_);

    reviewButton_ = new QPushButton(content);
    reviewButton_->setObjectName(QStringLiteral("practiceReviewButton"));
    reviewButton_->setMinimumHeight(44);
    connect(reviewButton_, &QPushButton::clicked, this, [this] {
        const auto questionId = currentQuestionId();
        if (!questionId.has_value()) {
            return;
        }
        emit reviewToggleRequested(*questionId, !reviewQuestionIds_.contains(*questionId));
    });

    aiButton_ = new QPushButton(QStringLiteral("AI解析"), content);
    aiButton_->setObjectName(QStringLiteral("practiceAiButton"));
    aiButton_->setMinimumHeight(44);
    connect(aiButton_, &QPushButton::clicked, this, [this] {
        const bool visible = !aiPanel_->isVisible();
        aiPanel_->setVisible(visible);
        aiButton_->setText(visible ? QStringLiteral("收起AI") : QStringLiteral("AI解析"));
    });
    auto *learningActions = new QHBoxLayout;
    learningActions->setContentsMargins(0, 0, 0, 0);
    learningActions->setSpacing(8);
    learningActions->addWidget(reviewButton_, 1);
    learningActions->addWidget(aiButton_, 1);
    contentLayout->addLayout(learningActions);

    answerSurface_ = new QFrame(content);
    answerSurface_->setObjectName(QStringLiteral("practiceAnswerSurface"));
    auto *answerLayout = new QVBoxLayout(answerSurface_);
    answerLayout->setContentsMargins(14, 12, 14, 12);
    answerLayout->setSpacing(8);
    answerLabel_ = new QLabel(answerSurface_);
    answerLabel_->setObjectName(QStringLiteral("practiceAnswerLabel"));
    answerLabel_->setWordWrap(true);
    explanationLabel_ = new QLabel(answerSurface_);
    explanationLabel_->setObjectName(QStringLiteral("practiceExplanationLabel"));
    explanationLabel_->setWordWrap(true);
    explanationLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    answerLayout->addWidget(answerLabel_);
    answerLayout->addWidget(explanationLabel_);
    explanationImagesLayout_ = new QVBoxLayout;
    explanationImagesLayout_->setContentsMargins(0, 0, 0, 0);
    explanationImagesLayout_->setSpacing(10);
    answerLayout->addLayout(explanationImagesLayout_);
    contentLayout->addWidget(answerSurface_);

    aiPanel_ = new QuestionAiPanel(content);
    aiPanel_->hide();
    connect(aiPanel_, &QuestionAiPanel::analyzeRequested,
            this, &PracticePage::aiAnalysisRequested);
    connect(aiPanel_, &QuestionAiPanel::cancelRequested,
            this, &PracticePage::aiAnalysisCancelRequested);
    contentLayout->addWidget(aiPanel_);

    contentLayout->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto *actions = new QFrame(this);
    actions->setObjectName(QStringLiteral("practiceActions"));
    auto *actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(14, 10, 14, 10);
    actionsLayout->setSpacing(8);

    previousButton_ = new QPushButton(QStringLiteral("上一题"), actions);
    previousButton_->setObjectName(QStringLiteral("practicePreviousButton"));
    previousButton_->setMinimumHeight(42);
    connect(previousButton_, &QPushButton::clicked, this, [this] {
        practiceService_.move(session_, session_.currentIndex - 1);
        render();
        emit sessionChanged();
    });
    actionsLayout->addWidget(previousButton_);

    revealButton_ = new QPushButton(QStringLiteral("看答案"), actions);
    revealButton_->setObjectName(QStringLiteral("practiceRevealButton"));
    revealButton_->setMinimumHeight(42);
    connect(revealButton_, &QPushButton::clicked, this, [this] {
        practiceService_.toggleReveal(session_);
        render();
        emit sessionChanged();
    });
    actionsLayout->addWidget(revealButton_);

    handwritingButton_ = new QPushButton(QStringLiteral("手写"), actions);
    handwritingButton_->setObjectName(QStringLiteral("practiceHandwritingButton"));
    handwritingButton_->setMinimumHeight(42);
    connect(handwritingButton_, &QPushButton::clicked, this, [this] {
        emit handwritingRequested(practiceService_.notebookContext(session_));
    });
    actionsLayout->addWidget(handwritingButton_);

    nextButton_ = new QPushButton(QStringLiteral("下一题"), actions);
    nextButton_->setObjectName(QStringLiteral("practiceNextButton"));
    nextButton_->setMinimumHeight(42);
    connect(nextButton_, &QPushButton::clicked, this, [this] {
        practiceService_.move(session_, session_.currentIndex + 1);
        render();
        emit sessionChanged();
    });
    actionsLayout->addWidget(nextButton_);

    root->addWidget(actions);
    updateResponsiveHeader();
    render();
}

void PracticePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResponsiveHeader();
}

void PracticePage::updateResponsiveHeader()
{
    const bool compact = width() < 620;
    clearLayoutItems(topLayout_);
    backButton_->setText(compact ? QStringLiteral("返回") : QStringLiteral("返回题库"));
    if (compact) {
        topLayout_->addWidget(backButton_, 0, 0);
        topLayout_->addWidget(bankTitle_, 0, 1);
        topLayout_->addWidget(headerActions_, 1, 0, 1, 2, Qt::AlignRight);
        topLayout_->setColumnStretch(0, 0);
        topLayout_->setColumnStretch(1, 1);
        topLayout_->setColumnStretch(2, 0);
    } else {
        topLayout_->addWidget(backButton_, 0, 0);
        topLayout_->addWidget(bankTitle_, 0, 1);
        topLayout_->addWidget(headerActions_, 0, 2);
        topLayout_->setColumnStretch(0, 0);
        topLayout_->setColumnStretch(1, 1);
        topLayout_->setColumnStretch(2, 0);
    }
}

void PracticePage::start(
    const domain::InstalledBankSummary &bank,
    const QVector<domain::Question> &questions,
    domain::PracticeMode mode,
    std::optional<domain::PracticeSession> restoredSession)
{
    bank_ = bank;
    questions_ = questions;
    renderedQuestionId_ = QUuid();
    renderedOptionTexts_.clear();
    renderedAnswerOnlyMode_ = false;
    questionIndexes_.clear();
    QVector<QUuid> questionOrder;
    questionOrder.reserve(questions_.size());
    for (qsizetype index = 0; index < questions_.size(); ++index) {
        questionOrder.append(questions_.at(index).id);
        questionIndexes_.insert(questions_.at(index).id, index);
    }
    if (restoredSession.has_value()
        && restoredSession->scopeId == bank.id
        && restoredSession->mode == mode) {
        session_ = *restoredSession;
        QVector<QUuid> reconciledOrder;
        QSet<QUuid> seen;
        for (const QUuid &questionId : session_.questionOrder) {
            if (questionIndexes_.contains(questionId) && !seen.contains(questionId)) {
                reconciledOrder.append(questionId);
                seen.insert(questionId);
            }
        }
        for (const QUuid &questionId : questionOrder) {
            if (!seen.contains(questionId)) {
                reconciledOrder.append(questionId);
            }
        }
        session_.questionOrder = reconciledOrder;
        if (session_.currentIndex < 0) {
            session_.currentIndex = 0;
        }
        if (!session_.questionOrder.isEmpty()
            && session_.currentIndex >= session_.questionOrder.size()) {
            session_.currentIndex = session_.questionOrder.size() - 1;
        }
    } else {
        session_ = practiceService_.start(bank.id, mode, questionOrder);
    }
    session_.viewport.insert(QStringLiteral("bankId"), bank.id);
    session_.viewport.insert(QStringLiteral("bankTitle"), bank.title);
    session_.dirty = false;
    render();
}

bool PracticePage::hasActiveSession() const
{
    return !session_.id.isNull() && !questions_.isEmpty();
}

domain::PracticeSession PracticePage::session() const
{
    return session_;
}

std::optional<QUuid> PracticePage::currentQuestionId() const
{
    return practiceService_.currentQuestionId(session_);
}

void PracticePage::restoreNotebookContext(const domain::NotebookLaunchContext &context)
{
    if (context.sessionId.isNull() || context.sessionId != session_.id) {
        return;
    }
    session_.currentIndex = context.questionIndex;
    session_.viewport = context.practiceViewport;
    render();
    emit sessionChanged();
}

void PracticePage::setDataRoot(const QString &dataRoot)
{
    if (dataRoot_ != dataRoot) {
        renderedQuestionId_ = QUuid();
    }
    dataRoot_ = dataRoot;
    render();
}

void PracticePage::setWrongBookQuestionIds(const QSet<QUuid> &questionIds)
{
    wrongBookQuestionIds_ = questionIds;
    render();
}

void PracticePage::setWrongBookMembership(const QUuid &questionId, bool included)
{
    if (included) {
        wrongBookQuestionIds_.insert(questionId);
    } else {
        wrongBookQuestionIds_.remove(questionId);
    }
    render();
}

void PracticePage::setReviewQuestionIds(const QSet<QUuid> &questionIds)
{
    reviewQuestionIds_ = questionIds;
    render();
}

void PracticePage::setReviewMembership(const QUuid &questionId, bool included)
{
    if (included) {
        reviewQuestionIds_.insert(questionId);
    } else {
        reviewQuestionIds_.remove(questionId);
    }
    render();
}

void PracticePage::applyPersistedAnswers(const QHash<QUuid, QString> &answers)
{
    if (session_.mode != domain::PracticeMode::Sequential
        && session_.mode != domain::PracticeMode::Random) return;
    for (auto iterator = answers.constBegin(); iterator != answers.constEnd(); ++iterator) {
        if (!questionIndexes_.contains(iterator.key())) continue;
        if (iterator.value().isEmpty()) session_.answers.remove(iterator.key());
        else session_.answers.insert(iterator.key(), iterator.value());
        session_.drafts.remove(iterator.key());
    }
    session_.dirty = false;
    render();
}

void PracticePage::applyMergedProgress(const domain::PracticeSession &progress)
{
    if (!hasActiveSession()) return;
    const QUuid progressQuestion = progress.currentIndex >= 0
            && progress.currentIndex < progress.questionOrder.size()
        ? progress.questionOrder.at(progress.currentIndex)
        : QUuid();
    session_.answers.clear();
    session_.drafts.clear();
    session_.revealedAnswers.clear();
    for (auto iterator = progress.answers.constBegin(); iterator != progress.answers.constEnd(); ++iterator) {
        if (questionIndexes_.contains(iterator.key())) {
            session_.answers.insert(iterator.key(), iterator.value());
        }
    }
    for (auto iterator = progress.drafts.constBegin(); iterator != progress.drafts.constEnd(); ++iterator) {
        if (questionIndexes_.contains(iterator.key())) {
            session_.drafts.insert(iterator.key(), iterator.value());
        }
    }
    for (const QUuid &questionId : progress.revealedAnswers) {
        if (questionIndexes_.contains(questionId)) {
            session_.revealedAnswers.insert(questionId);
        }
    }
    const qsizetype mergedIndex = session_.questionOrder.indexOf(progressQuestion);
    session_.currentIndex = mergedIndex >= 0 ? mergedIndex : 0;
    session_.dirty = false;
    render();
}

void PracticePage::resetSession(bool reshuffleRandom)
{
    if (!hasActiveSession()) return;
    const domain::PracticeMode mode = session_.mode;
    QVector<QUuid> order;
    order.reserve(questions_.size());
    for (const domain::Question &question : questions_) {
        order.append(question.id);
    }
    if (mode == domain::PracticeMode::Random && !reshuffleRandom) {
        order = session_.questionOrder;
    }
    session_ = practiceService_.start(bank_.id, mode, order);
    if (mode == domain::PracticeMode::Random && !reshuffleRandom) {
        session_.questionOrder = order;
    }
    session_.viewport.insert(QStringLiteral("bankId"), bank_.id);
    session_.viewport.insert(QStringLiteral("bankTitle"), bank_.title);
    session_.dirty = true;
    renderedQuestionId_ = QUuid();
    render();
    emit sessionChanged();
}

void PracticePage::showSaveStatus(const QString &message, bool error)
{
    saveStatusLabel_->setText(message);
    saveStatusLabel_->setProperty("error", error);
    saveStatusLabel_->style()->unpolish(saveStatusLabel_);
    saveStatusLabel_->style()->polish(saveStatusLabel_);
    saveStatusLabel_->show();
    QTimer::singleShot(1600, saveStatusLabel_, [label = saveStatusLabel_, message] {
        if (label->text() == message) {
            label->hide();
        }
    });
}

void PracticePage::setAiRecord(const domain::AiRecord &record)
{
    aiPanel_->setRecord(record);
}

void PracticePage::setAiLoading(const QUuid &questionId)
{
    aiPanel_->setVisible(true);
    aiButton_->setText(QStringLiteral("收起AI"));
    aiPanel_->setLoading(questionId);
}

void PracticePage::setAiError(const QUuid &questionId, const QString &message)
{
    aiPanel_->setVisible(true);
    aiButton_->setText(QStringLiteral("收起AI"));
    aiPanel_->setError(questionId, message);
}

void PracticePage::setAiCancelled(const QUuid &questionId)
{
    aiPanel_->setCancelled(questionId);
}

bool PracticePage::selectQuestion(qsizetype questionIndex, bool renderPage)
{
    if (!practiceService_.move(session_, questionIndex)) {
        return false;
    }
    if (renderPage) {
        render();
    }
    emit sessionChanged();
    return true;
}

domain::NotebookLaunchContext PracticePage::notebookContext() const
{
    return practiceService_.notebookContext(session_);
}

void PracticePage::render()
{
    if (!hasActiveSession()) {
        renderedQuestionId_ = QUuid();
        bankTitle_->setText(QStringLiteral("暂无练习"));
        progressLabel_->clear();
        typeLabel_->clear();
        promptLabel_->setText(QStringLiteral("请先从题库页选择一个题库开始练习。"));
        clearOptions();
        clearImageLayout(questionImagesLayout_);
        clearImageLayout(explanationImagesLayout_);
        answerLabel_->clear();
        explanationLabel_->clear();
        answerSurface_->hide();
        aiPanel_->hide();
        aiButton_->setText(QStringLiteral("AI解析"));
        wrongBookButton_->hide();
        reviewButton_->hide();
        updateNavigationButtons();
        return;
    }

    const domain::Question *question = currentQuestion();
    if (!question) {
        renderedQuestionId_ = QUuid();
        promptLabel_->setText(QStringLiteral("当前题目不存在或已被题库更新移除。"));
        clearOptions();
        clearImageLayout(questionImagesLayout_);
        clearImageLayout(explanationImagesLayout_);
        answerLabel_->clear();
        explanationLabel_->clear();
        answerSurface_->hide();
        aiPanel_->hide();
        aiButton_->setText(QStringLiteral("AI解析"));
        reviewButton_->hide();
        updateNavigationButtons();
        return;
    }

    bankTitle_->setText(joinPath(bank_.path).isEmpty()
        ? bank_.title : joinPath(bank_.path));
    progressLabel_->setText(QStringLiteral("%1/%2")
        .arg(session_.currentIndex + 1)
        .arg(session_.questionOrder.size()));
    typeLabel_->setText(questionTypeText(question->type));
    typeLabel_->setText(QStringLiteral("%1 · %2")
        .arg(practiceModeText(session_.mode), questionTypeText(question->type)));
    const bool questionChanged = renderedQuestionId_ != question->id;
    if (questionChanged) {
        aiPanel_->setQuestion(*question);
    }
    promptLabel_->setText(question->prompt);
    if (questionChanged) {
        clearImageLayout(questionImagesLayout_);
        addImages(questionImagesLayout_, *question, question->questionImageBlobIds);
        clearImageLayout(explanationImagesLayout_);
    }

    const QString selected = selectedAnswerForCurrentQuestion();
    const bool answerOnlyMode = session_.mode == domain::PracticeMode::Memorize
        || session_.mode == domain::PracticeMode::AnswerLookup;
    const bool revealed = session_.revealedAnswers.contains(question->id);
    QStringList options = question->options;
    if (question->type == domain::QuestionType::Boolean && options.isEmpty()) {
        options = {QStringLiteral("正确"), QStringLiteral("错误")};
    }
    if (questionChanged || renderedOptionTexts_ != options
        || renderedAnswerOnlyMode_ != answerOnlyMode) {
        clearOptions();
        for (qsizetype index = 0; index < options.size(); ++index) {
            QPushButton *button = createOptionButton(
                optionKey(index), options.at(index), selected);
            optionButtons_.append(button);
            optionsLayout_->addWidget(button);
        }
        renderedOptionTexts_ = options;
        renderedAnswerOnlyMode_ = answerOnlyMode;
    }
    updateOptionStates(*question, selected, answerOnlyMode, revealed);
    renderedQuestionId_ = question->id;

    revealButton_->setEnabled(!answerOnlyMode);
    revealButton_->setText(answerOnlyMode
        ? QStringLiteral("答案已显示")
        : (revealed ? QStringLiteral("收起答案") : QStringLiteral("看答案")));
    answerSurface_->setVisible(revealed || answerOnlyMode);
    const bool inWrongBook = wrongBookQuestionIds_.contains(question->id);
    const bool showWrongBookAction = !answerOnlyMode
        && (session_.mode == domain::PracticeMode::WrongBook
            || inWrongBook || currentAnswerIsWrong());
    wrongBookButton_->setVisible(showWrongBookAction);
    wrongBookButton_->setText(inWrongBook
        ? QStringLiteral("移出错题集") : QStringLiteral("加入错题集"));
    reviewButton_->show();
    reviewButton_->setText(reviewQuestionIds_.contains(question->id)
        ? QStringLiteral("移出复习") : QStringLiteral("加入复习"));
    if (revealed || answerOnlyMode) {
        if (question->correctAnswer.trimmed().isEmpty()) {
            answerLabel_->setText(
                question->builtinExplanation.text.trimmed().isEmpty()
                    && question->builtinExplanation.imageBlobIds.isEmpty()
                ? QStringLiteral("暂无标准答案")
                : QStringLiteral("正确答案：见内置解析"));
        } else {
            answerLabel_->setText(QStringLiteral("正确答案：%1").arg(question->correctAnswer));
        }
        explanationLabel_->setText(question->builtinExplanation.text.trimmed().isEmpty()
            ? QStringLiteral("暂无内置解析")
            : question->builtinExplanation.text);
        if (questionChanged || explanationImagesLayout_->count() == 0) {
            clearImageLayout(explanationImagesLayout_);
            addImages(
                explanationImagesLayout_, *question,
                question->builtinExplanation.imageBlobIds);
        }
    } else {
        answerLabel_->clear();
        explanationLabel_->clear();
    }
    updateNavigationButtons();
    if (questionChanged) emit currentQuestionChanged(*question);
}

const domain::Question *PracticePage::currentQuestion() const
{
    const auto questionId = currentQuestionId();
    if (!questionId.has_value()) {
        return nullptr;
    }
    const auto found = questionIndexes_.constFind(*questionId);
    if (found == questionIndexes_.cend()) {
        return nullptr;
    }
    return &questions_.at(found.value());
}

QString PracticePage::selectedAnswerForCurrentQuestion() const
{
    const auto questionId = currentQuestionId();
    if (!questionId.has_value()) {
        return {};
    }
    if (session_.drafts.contains(*questionId)) {
        return session_.drafts.value(*questionId);
    }
    return session_.answers.value(*questionId);
}

QString PracticePage::optionKey(qsizetype optionIndex) const
{
    return QString(QChar(static_cast<char16_t>(u'A' + optionIndex)));
}

QString PracticePage::questionTypeText(domain::QuestionType type) const
{
    switch (type) {
    case domain::QuestionType::Single:
        return QStringLiteral("单选题");
    case domain::QuestionType::Multiple:
        return QStringLiteral("多选题");
    case domain::QuestionType::Boolean:
        return QStringLiteral("判断题");
    case domain::QuestionType::Subjective:
        return QStringLiteral("主观题");
    }
    return {};
}

QString PracticePage::practiceModeText(domain::PracticeMode mode) const
{
    switch (mode) {
    case domain::PracticeMode::Sequential:
        return QStringLiteral("顺序练习");
    case domain::PracticeMode::Random:
        return QStringLiteral("随机练习");
    case domain::PracticeMode::Memorize:
        return QStringLiteral("背题模式");
    case domain::PracticeMode::AnswerLookup:
        return QStringLiteral("答案表");
    case domain::PracticeMode::WrongBook:
        return QStringLiteral("错题集");
    case domain::PracticeMode::Review:
        return QStringLiteral("复习");
    }
    return {};
}

void PracticePage::clearOptions()
{
    optionButtons_.clear();
    renderedOptionTexts_.clear();
    while (optionsLayout_->count() > 0) {
        QLayoutItem *item = optionsLayout_->takeAt(0);
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void PracticePage::clearImageLayout(QVBoxLayout *layout)
{
    if (!layout) {
        return;
    }
    while (layout->count() > 0) {
        QLayoutItem *item = layout->takeAt(0);
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void PracticePage::addImages(
    QVBoxLayout *layout,
    const domain::Question &question,
    const QStringList &blobIds)
{
    if (!layout || dataRoot_.trimmed().isEmpty()) {
        return;
    }
    const int maximumWidth = qBound(280, width() - 64, 860);
    for (const QString &blobId : blobIds) {
        const QString relativePath = question.blobRelativePaths.value(blobId);
        if (relativePath.isEmpty()) {
            continue;
        }
        const QPixmap pixmap(QDir(dataRoot_).filePath(relativePath));
        if (pixmap.isNull()) {
            continue;
        }
        auto *label = new QLabel(this);
        label->setObjectName(QStringLiteral("practiceImage"));
        label->setAlignment(Qt::AlignCenter);
        const int targetWidth = qMin(maximumWidth, pixmap.width());
        label->setPixmap(
            targetWidth < pixmap.width()
                ? pixmap.scaledToWidth(targetWidth, Qt::SmoothTransformation)
                : pixmap);
        layout->addWidget(label);
    }
}

QPushButton *PracticePage::createOptionButton(
    const QString &key,
    const QString &text,
    const QString &selectedAnswer)
{
    auto *button = new QPushButton(QStringLiteral("%1. %2").arg(key, text), this);
    button->setObjectName(QStringLiteral("practiceOptionButton"));
    button->setCheckable(true);
    button->setChecked(selectedAnswer.contains(key));
    button->setMinimumHeight(44);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    const bool answerOnlyMode = session_.mode == domain::PracticeMode::Memorize
        || session_.mode == domain::PracticeMode::AnswerLookup;
    button->setEnabled(!answerOnlyMode);
    connect(button, &QPushButton::clicked, this, [this, key] {
        const domain::Question *question = currentQuestion();
        if (!question) {
            return;
        }
        if (question->type == domain::QuestionType::Multiple) {
            practiceService_.toggleDraftOption(session_, key.at(0));
        } else {
            practiceService_.selectAnswer(session_, key);
        }
        render();
        emit sessionChanged();
    });
    return button;
}

void PracticePage::updateNavigationButtons()
{
    const bool active = hasActiveSession();
    previousButton_->setEnabled(active && session_.currentIndex > 0);
    nextButton_->setEnabled(active && session_.currentIndex + 1 < session_.questionOrder.size());
    revealButton_->setEnabled(active);
    handwritingButton_->setEnabled(active && currentQuestionId().has_value());
    aiButton_->setEnabled(active && currentQuestionId().has_value());
    overviewButton_->setEnabled(active);
}

void PracticePage::showQuestionOverview()
{
    if (!hasActiveSession()) {
        return;
    }
    auto *dialog = new QuestionOverviewDialog(questions_, session_, this);
    connect(dialog, &QuestionOverviewDialog::questionSelected,
            this, &PracticePage::moveToQuestion);
    dialog->open();
}

void PracticePage::moveToQuestion(qsizetype questionIndex)
{
    selectQuestion(questionIndex, true);
}

bool PracticePage::currentAnswerIsWrong() const
{
    const domain::Question *question = currentQuestion();
    if (!question || question->type == domain::QuestionType::Subjective
        || question->correctAnswer.trimmed().isEmpty()) {
        return false;
    }
    const QString answer = selectedAnswerForCurrentQuestion().trimmed().toUpper();
    return !answer.isEmpty()
        && answer != question->correctAnswer.trimmed().toUpper();
}

void PracticePage::updateOptionStates(
    const domain::Question &question,
    const QString &selectedAnswer,
    bool answerOnlyMode,
    bool revealed)
{
    const QString correctAnswer = question.correctAnswer.trimmed().toUpper();
    const bool gradeImmediately = question.type == domain::QuestionType::Single
        || question.type == domain::QuestionType::Boolean;
    for (qsizetype index = 0; index < optionButtons_.size(); ++index) {
        QPushButton *button = optionButtons_.at(index);
        const QString key = optionKey(index);
        const bool selected = selectedAnswer.contains(key);
        QString state = QStringLiteral("idle");
        if (selected) {
            if (gradeImmediately && !correctAnswer.isEmpty()) {
                state = selectedAnswer == correctAnswer
                    ? QStringLiteral("correct") : QStringLiteral("wrong");
            } else {
                state = QStringLiteral("selected");
            }
        } else if ((revealed || answerOnlyMode) && correctAnswer.contains(key)) {
            state = QStringLiteral("correctReveal");
        }
        button->setChecked(selected);
        button->setEnabled(!answerOnlyMode);
        if (button->property("answerState").toString() != state) {
            button->setProperty("answerState", state);
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
    }
}

} // namespace quizapp::ui
