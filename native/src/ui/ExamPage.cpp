#include "ui/ExamPage.h"

#include "storage/Database.h"
#include "storage/SqliteExamRepository.h"
#include "ui/ChoiceComboBox.h"

#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace quizapp::ui {
namespace {

QString questionTypeText(domain::QuestionType type)
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
    return QStringLiteral("题目");
}

QString connectionName(const QString &prefix)
{
    return QStringLiteral("%1-%2")
        .arg(prefix, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QWidget *centeredScrollPage(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setObjectName(QStringLiteral("examPageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *container = new QWidget(scroll);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();
    content->setMaximumWidth(860);
    layout->addWidget(content, 1);
    layout->addStretch();
    scroll->setWidget(container);
    return scroll;
}

} // namespace

ExamPage::ExamPage(QString databasePath, QWidget *parent)
    : QWidget(parent)
    , databasePath_(std::move(databasePath))
{
    setObjectName(QStringLiteral("examPageRoot"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    pages_ = new QStackedWidget(this);
    setupPage_ = createSetupPage();
    examPage_ = createExamPage();
    resultPage_ = createResultPage();
    pages_->addWidget(setupPage_);
    pages_->addWidget(examPage_);
    pages_->addWidget(resultPage_);
    layout->addWidget(pages_);

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (!activeSession_ || activeSession_->paused) {
            return;
        }
        service_.advanceTimer(*activeSession_, 1);
        ++ticksSinceSave_;
        timerLabel_->setText(formattedTime(activeSession_->remainingSeconds));
        if (ticksSinceSave_ >= 10) {
            saveActiveExam();
        }
        if (activeSession_->remainingSeconds <= 0) {
            submitExam(true);
        }
    });
    showSetup();
}

QWidget *ExamPage::createSetupPage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(22, 22, 22, 32);
    layout->setSpacing(14);
    auto *header = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("返回"), content);
    back->setObjectName(QStringLiteral("examSetupBackButton"));
    connect(back, &QPushButton::clicked, this, &ExamPage::backRequested);
    auto *title = new QLabel(QStringLiteral("模拟考试"), content);
    title->setObjectName(QStringLiteral("pageHeading"));
    header->addWidget(back);
    header->addWidget(title);
    header->addStretch();
    layout->addLayout(header);

    resumeButton_ = new QPushButton(QStringLiteral("继续未完成考试"), content);
    resumeButton_->setObjectName(QStringLiteral("examResumeButton"));
    resumeButton_->setMinimumHeight(54);
    connect(resumeButton_, &QPushButton::clicked, this, &ExamPage::resumeActiveExam);
    layout->addWidget(resumeButton_);

    auto *setup = new QFrame(content);
    setup->setObjectName(QStringLiteral("examSetupSurface"));
    auto *form = new QGridLayout(setup);
    form->setContentsMargins(16, 16, 16, 16);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(10);
    scopeChoice_ = new ChoiceComboBox(setup);
    scopeChoice_->setObjectName(QStringLiteral("examScopeChoice"));
    countChoice_ = new ChoiceComboBox(setup);
    countChoice_->setObjectName(QStringLiteral("examCountChoice"));
    countChoice_->addItem(QStringLiteral("20 题"), 20);
    countChoice_->addItem(QStringLiteral("50 题"), 50);
    countChoice_->addItem(QStringLiteral("100 题"), 100);
    countChoice_->addItem(QStringLiteral("全部可判分题"), 0);
    durationChoice_ = new ChoiceComboBox(setup);
    durationChoice_->setObjectName(QStringLiteral("examDurationChoice"));
    durationChoice_->addItem(QStringLiteral("30 分钟"), 30);
    durationChoice_->addItem(QStringLiteral("60 分钟"), 60);
    durationChoice_->addItem(QStringLiteral("120 分钟"), 120);
    const std::array<std::pair<QString, QComboBox *>, 3> fields{{
        {QStringLiteral("考试范围"), scopeChoice_},
        {QStringLiteral("题量"), countChoice_},
        {QStringLiteral("时长"), durationChoice_},
    }};
    for (qsizetype row = 0; row < fields.size(); ++row) {
        auto *label = new QLabel(fields.at(row).first, setup);
        label->setObjectName(QStringLiteral("settingsFieldLabel"));
        fields.at(row).second->setMinimumHeight(42);
        form->addWidget(label, row, 0);
        form->addWidget(fields.at(row).second, row, 1);
    }
    auto *hint = new QLabel(
        QStringLiteral("考试只抽取可自动判分的单选、多选和判断题，交卷前不显示答案。"),
        setup);
    hint->setObjectName(QStringLiteral("pageSupportingText"));
    hint->setWordWrap(true);
    form->addWidget(hint, 3, 0, 1, 2);
    auto *start = new QPushButton(QStringLiteral("开始考试"), setup);
    start->setObjectName(QStringLiteral("examStartButton"));
    start->setMinimumHeight(48);
    connect(start, &QPushButton::clicked, this, &ExamPage::startConfiguredExam);
    form->addWidget(start, 4, 0, 1, 2);
    form->setColumnStretch(1, 1);
    layout->addWidget(setup);

    setupStatus_ = new QLabel(content);
    setupStatus_->setObjectName(QStringLiteral("examSetupStatus"));
    setupStatus_->setWordWrap(true);
    layout->addWidget(setupStatus_);
    auto *historyTitle = new QLabel(QStringLiteral("最近考试"), content);
    historyTitle->setObjectName(QStringLiteral("sectionHeading"));
    layout->addWidget(historyTitle);
    historyList_ = new QListWidget(content);
    historyList_->setObjectName(QStringLiteral("examHistoryList"));
    historyList_->setMinimumHeight(180);
    connect(historyList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QUuid id(item->data(Qt::UserRole).toString());
        storage::Database database(connectionName(QStringLiteral("exam-history-open")));
        QString error;
        if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
            setStatus(QStringLiteral("读取考试记录失败：%1").arg(error), true);
            return;
        }
        storage::SqliteExamRepository repository(database.connection());
        const auto session = repository.load(id, &error);
        if (!session) {
            setStatus(QStringLiteral("读取考试记录失败：%1").arg(error), true);
            return;
        }
        renderResult(*session, true);
    });
    layout->addWidget(historyList_);
    layout->addStretch();
    return centeredScrollPage(content, this);
}

QWidget *ExamPage::createExamPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);
    examHeaderLayout_ = new QGridLayout;
    examHeaderLayout_->setContentsMargins(0, 0, 0, 0);
    examHeaderLayout_->setSpacing(8);
    examBackButton_ = new QPushButton(QStringLiteral("返回"), page);
    examBackButton_->setObjectName(QStringLiteral("examBackButton"));
    connect(examBackButton_, &QPushButton::clicked, this, &ExamPage::requestExitExam);
    examTitle_ = new QLabel(page);
    examTitle_->setObjectName(QStringLiteral("sectionHeading"));
    pauseButton_ = new QPushButton(QStringLiteral("暂停"), page);
    pauseButton_->setObjectName(QStringLiteral("examPauseButton"));
    connect(pauseButton_, &QPushButton::clicked, this, [this] {
        if (!activeSession_) {
            return;
        }
        service_.setPaused(*activeSession_, !activeSession_->paused);
        saveActiveExam();
        renderExam();
    });
    timerLabel_ = new QLabel(page);
    timerLabel_->setObjectName(QStringLiteral("examTimerLabel"));
    timerLabel_->setMinimumWidth(72);
    timerLabel_->setAlignment(Qt::AlignCenter);
    submitButton_ = new QPushButton(QStringLiteral("交卷"), page);
    submitButton_->setObjectName(QStringLiteral("examSubmitButton"));
    connect(submitButton_, &QPushButton::clicked, this, [this] { submitExam(false); });
    layout->addLayout(examHeaderLayout_);
    applyResponsiveHeader();
    progress_ = new QProgressBar(page);
    progress_->setObjectName(QStringLiteral("examProgress"));
    progress_->setTextVisible(false);
    layout->addWidget(progress_);

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(8, 8, 8, 16);
    contentLayout->setSpacing(12);
    questionMeta_ = new QLabel(content);
    questionMeta_->setObjectName(QStringLiteral("pageSupportingText"));
    questionPrompt_ = new QLabel(content);
    questionPrompt_->setObjectName(QStringLiteral("examQuestionPrompt"));
    questionPrompt_->setWordWrap(true);
    contentLayout->addWidget(questionMeta_);
    contentLayout->addWidget(questionPrompt_);
    optionsLayout_ = new QGridLayout;
    optionsLayout_->setSpacing(9);
    contentLayout->addLayout(optionsLayout_);
    pauseCover_ = new QLabel(QStringLiteral("考试已暂停，点击上方“继续”恢复作答。"), content);
    pauseCover_->setObjectName(QStringLiteral("examPauseCover"));
    pauseCover_->setAlignment(Qt::AlignCenter);
    pauseCover_->setWordWrap(true);
    pauseCover_->setMinimumHeight(130);
    contentLayout->addWidget(pauseCover_);
    contentLayout->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    auto *bottom = new QHBoxLayout;
    previousButton_ = new QPushButton(QStringLiteral("上一题"), page);
    auto *overview = new QPushButton(QStringLiteral("题号总览"), page);
    overview->setObjectName(QStringLiteral("examOverviewButton"));
    auto *handwriting = new QPushButton(QStringLiteral("手写"), page);
    handwriting->setObjectName(QStringLiteral("examHandwritingButton"));
    nextButton_ = new QPushButton(QStringLiteral("下一题"), page);
    connect(previousButton_, &QPushButton::clicked, this, [this] {
        if (activeSession_ && service_.move(*activeSession_, activeSession_->currentIndex - 1)) {
            saveActiveExam();
            renderExam();
        }
    });
    connect(nextButton_, &QPushButton::clicked, this, [this] {
        if (activeSession_ && service_.move(*activeSession_, activeSession_->currentIndex + 1)) {
            saveActiveExam();
            renderExam();
        }
    });
    connect(overview, &QPushButton::clicked, this, &ExamPage::showOverview);
    connect(handwriting, &QPushButton::clicked, this, [this] {
        if (!activeSession_ || activeSession_->questionOrder.isEmpty()) {
            return;
        }
        saveActiveExam();
        domain::NotebookLaunchContext context;
        context.sessionId = activeSession_->id;
        context.questionId = activeSession_->questionOrder.at(activeSession_->currentIndex);
        context.questionIndex = activeSession_->currentIndex;
        context.practiceMode = domain::PracticeMode::Review;
        emit handwritingRequested(context);
    });
    bottom->addWidget(previousButton_);
    bottom->addWidget(overview);
    bottom->addWidget(handwriting);
    bottom->addWidget(nextButton_);
    layout->addLayout(bottom);
    return page;
}

QWidget *ExamPage::createResultPage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(22, 22, 22, 32);
    layout->setSpacing(12);
    auto *header = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("返回考试列表"), content);
    back->setObjectName(QStringLiteral("examResultBackButton"));
    connect(back, &QPushButton::clicked, this, &ExamPage::showSetup);
    auto *title = new QLabel(QStringLiteral("考试结果"), content);
    title->setObjectName(QStringLiteral("pageHeading"));
    header->addWidget(back);
    header->addWidget(title);
    header->addStretch();
    layout->addLayout(header);
    resultScore_ = new QLabel(content);
    resultScore_->setObjectName(QStringLiteral("examResultScore"));
    resultScore_->setAlignment(Qt::AlignCenter);
    resultScore_->setMinimumHeight(64);
    resultSummary_ = new QLabel(content);
    resultSummary_->setObjectName(QStringLiteral("pageSupportingText"));
    resultSummary_->setAlignment(Qt::AlignCenter);
    resultSummary_->setWordWrap(true);
    resultSummary_->setMinimumHeight(54);
    layout->addWidget(resultScore_);
    layout->addWidget(resultSummary_);
    resultList_ = new QListWidget(content);
    resultList_->setObjectName(QStringLiteral("examResultList"));
    resultList_->setWordWrap(true);
    resultList_->setMinimumHeight(320);
    layout->addWidget(resultList_, 1);
    return centeredScrollPage(content, this);
}

void ExamPage::setQuestions(const QVector<domain::Question> &questions)
{
    questions_.clear();
    questionsById_.clear();
    for (const domain::Question &question : questions) {
        if (services::ExamService::isObjective(question)
            && !questionsById_.contains(question.id)) {
            questions_.append(question);
            questionsById_.insert(question.id, question);
        }
    }
    rebuildScopeChoices();
    if (pages_->currentWidget() == setupPage_) {
        refreshSetup();
    }
}

void ExamPage::rebuildScopeChoices()
{
    const QString selected = scopeChoice_->currentData().toString();
    scopeChoice_->clear();
    scopeChoice_->addItem(QStringLiteral("全部题库"), QStringLiteral("all"));
    QStringList subjects;
    for (const domain::Question &question : std::as_const(questions_)) {
        if (!question.path.isEmpty() && !subjects.contains(question.path.first())) {
            subjects.append(question.path.first());
        }
    }
    subjects.sort(Qt::CaseInsensitive);
    for (const QString &subject : std::as_const(subjects)) {
        scopeChoice_->addItem(subject, subject);
    }
    const int index = scopeChoice_->findData(selected);
    scopeChoice_->setCurrentIndex(index >= 0 ? index : 0);
}

void ExamPage::showSetup()
{
    timer_->stop();
    activeSession_.reset();
    emit examActivityChanged(false);
    pages_->setCurrentWidget(setupPage_);
    refreshSetup();
}

void ExamPage::refreshSetup()
{
    QString error;
    const auto active = loadActive(&error);
    resumeButton_->setVisible(active.has_value());
    resumeButton_->setText(active
        ? QStringLiteral("继续未完成考试 · %1/%2 题")
              .arg(active->answers.size())
              .arg(active->questionOrder.size())
        : QStringLiteral("继续未完成考试"));
    historyList_->clear();
    const auto history = loadHistory(&error);
    for (const domain::ExamSession &session : history) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n%2 分 · %3/%4 题正确 · %5")
                .arg(session.title)
                .arg(session.score)
                .arg(session.correctCount)
                .arg(session.questionOrder.size())
                .arg(session.submittedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))),
            historyList_);
        item->setData(Qt::UserRole, session.id.toString(QUuid::WithoutBraces));
        item->setSizeHint(QSize(0, 62));
    }
    setStatus(error.isEmpty()
        ? QStringLiteral("当前有 %1 道可自动判分题目").arg(questions_.size())
        : QStringLiteral("读取考试记录失败：%1").arg(error), !error.isEmpty());
}

void ExamPage::startConfiguredExam()
{
    const QString scope = scopeChoice_->currentData().toString();
    QVector<domain::Question> available;
    for (const domain::Question &question : std::as_const(questions_)) {
        if (scope == QStringLiteral("all")
            || (!question.path.isEmpty() && question.path.first() == scope)) {
            available.append(question);
        }
    }
    if (available.isEmpty()) {
        setStatus(QStringLiteral("当前范围没有可自动判分的题目"), true);
        return;
    }
    const int count = countChoice_->currentData().toInt();
    const int minutes = durationChoice_->currentData().toInt();
    activeSession_ = service_.start(
        scope,
        scope == QStringLiteral("all")
            ? QStringLiteral("综合模拟考试") : QStringLiteral("%1 模拟考试").arg(scope),
        available,
        count,
        minutes * 60);
    saveActiveExam();
    renderExam();
}

void ExamPage::resumeActiveExam()
{
    QString error;
    activeSession_ = loadActive(&error);
    if (!activeSession_) {
        setStatus(error.isEmpty() ? QStringLiteral("没有可恢复的考试") : error, true);
        return;
    }
    for (const QUuid &id : std::as_const(activeSession_->questionOrder)) {
        if (!questionsById_.contains(id)) {
            storage::Database database(connectionName(QStringLiteral("exam-invalid-remove")));
            QString removeError;
            if (database.open(databasePath_, &removeError)
                && database.migrate(&removeError)) {
                storage::SqliteExamRepository repository(database.connection());
                repository.remove(activeSession_->id, &removeError);
            }
            activeSession_.reset();
            setStatus(QStringLiteral("部分考试题目已不在当前题库，无法恢复"), true);
            refreshSetup();
            return;
        }
    }
    renderExam();
}

void ExamPage::renderExam()
{
    if (!activeSession_ || activeSession_->questionOrder.isEmpty()) {
        showSetup();
        return;
    }
    pages_->setCurrentWidget(examPage_);
    emit examActivityChanged(true);
    examTitle_->setText(activeSession_->title);
    timerLabel_->setText(formattedTime(activeSession_->remainingSeconds));
    pauseButton_->setText(activeSession_->paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    const int index = static_cast<int>(activeSession_->currentIndex);
    const QUuid id = activeSession_->questionOrder.at(index);
    const domain::Question question = questionsById_.value(id);
    questionMeta_->setText(QStringLiteral("第 %1/%2 题 · %3 · 已答 %4")
        .arg(index + 1)
        .arg(activeSession_->questionOrder.size())
        .arg(questionTypeText(question.type))
        .arg(activeSession_->answers.size()));
    questionPrompt_->setText(question.prompt);
    progress_->setRange(0, activeSession_->questionOrder.size());
    progress_->setValue(activeSession_->answers.size());
    while (optionsLayout_->count() > 0) {
        QLayoutItem *item = optionsLayout_->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    optionButtons_.clear();
    const QString answer = activeSession_->answers.value(id);
    const int columns = width() >= 760 && question.options.size() <= 4 ? 2 : 1;
    for (qsizetype optionIndex = 0; optionIndex < question.options.size(); ++optionIndex) {
        const QChar letter(static_cast<char16_t>(u'A' + optionIndex));
        auto *button = new QPushButton(
            QStringLiteral("%1  %2").arg(letter).arg(question.options.at(optionIndex)), examPage_);
        button->setObjectName(QStringLiteral("examOptionButton"));
        button->setCheckable(true);
        button->setChecked(answer.contains(letter));
        button->setMinimumHeight(48);
        button->setEnabled(!activeSession_->paused);
        connect(button, &QPushButton::clicked, this, [this, letter] { selectOption(letter); });
        optionsLayout_->addWidget(
            button, static_cast<int>(optionIndex) / columns,
            static_cast<int>(optionIndex) % columns);
        optionButtons_.append(button);
    }
    pauseCover_->setVisible(activeSession_->paused);
    questionMeta_->setVisible(!activeSession_->paused);
    questionPrompt_->setVisible(!activeSession_->paused);
    previousButton_->setEnabled(!activeSession_->paused && index > 0);
    nextButton_->setEnabled(
        !activeSession_->paused && index + 1 < activeSession_->questionOrder.size());
    if (activeSession_->paused) {
        timer_->stop();
    } else {
        timer_->start();
    }
}

void ExamPage::selectOption(QChar option)
{
    if (!activeSession_) {
        return;
    }
    const QUuid id = activeSession_->questionOrder.at(activeSession_->currentIndex);
    if (service_.selectAnswer(*activeSession_, questionsById_.value(id), option)) {
        saveActiveExam();
        renderExam();
    }
}

void ExamPage::saveActiveExam()
{
    if (!activeSession_ || databasePath_.isEmpty()) {
        return;
    }
    storage::Database database(connectionName(QStringLiteral("exam-save")));
    QString error;
    if (!database.open(databasePath_, &error) || !database.migrate(&error)) {
        return;
    }
    storage::SqliteExamRepository repository(database.connection());
    if (repository.save(*activeSession_, &error)) {
        ticksSinceSave_ = 0;
    }
}

void ExamPage::submitExam(bool timedOut)
{
    if (!activeSession_) {
        return;
    }
    if (!timedOut && QMessageBox::question(
            this,
            QStringLiteral("确认交卷"),
            QStringLiteral("已答 %1/%2 题，确认交卷？")
                .arg(activeSession_->answers.size())
                .arg(activeSession_->questionOrder.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    timer_->stop();
    if (!service_.submit(*activeSession_, currentQuestionMap(), timedOut)) {
        QMessageBox::warning(this, QStringLiteral("交卷失败"),
                             QStringLiteral("考试题目已发生变化，无法完成评分。"));
        return;
    }
    saveActiveExam();
    const domain::ExamSession result = *activeSession_;
    activeSession_.reset();
    emit examActivityChanged(false);
    renderResult(result, false);
}

void ExamPage::requestExitExam()
{
    if (!activeSession_) {
        showSetup();
        return;
    }
    if (QMessageBox::question(
            this,
            QStringLiteral("退出考试"),
            QStringLiteral("退出后会保留当前考试，可以下次继续。"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    saveActiveExam();
    timer_->stop();
    activeSession_.reset();
    emit examActivityChanged(false);
    emit backRequested();
}

void ExamPage::showOverview()
{
    if (!activeSession_) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("题号总览"));
    dialog.resize(width() < 760 ? 350 : 620, width() < 760 ? 520 : 440);
    auto *layout = new QVBoxLayout(&dialog);
    auto *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    auto *grid = new QGridLayout(content);
    const int columns = width() < 760 ? 6 : 10;
    for (qsizetype index = 0; index < activeSession_->questionOrder.size(); ++index) {
        auto *button = new QPushButton(QString::number(index + 1), content);
        button->setObjectName(QStringLiteral("examOverviewNumber"));
        button->setCheckable(true);
        button->setChecked(index == activeSession_->currentIndex);
        button->setProperty(
            "answered", activeSession_->answers.contains(activeSession_->questionOrder.at(index)));
        button->setFixedSize(44, 40);
        connect(button, &QPushButton::clicked, &dialog, [this, index, &dialog] {
            if (service_.move(*activeSession_, index)) {
                saveActiveExam();
                dialog.accept();
                renderExam();
            }
        });
        grid->addWidget(button, static_cast<int>(index) / columns,
                        static_cast<int>(index) % columns);
    }
    scroll->setWidget(content);
    layout->addWidget(scroll);
    auto *close = new QPushButton(QStringLiteral("关闭"), &dialog);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    layout->addWidget(close);
    dialog.exec();
}

void ExamPage::renderResult(const domain::ExamSession &session, bool fromHistory)
{
    timer_->stop();
    pages_->setCurrentWidget(resultPage_);
    resultScore_->setText(QStringLiteral("%1 分").arg(session.score));
    resultSummary_->setText(QStringLiteral("%1\n正确 %2 · 错误 %3 · 未答 %4%5")
        .arg(session.title)
        .arg(session.correctCount)
        .arg(session.wrongCount)
        .arg(session.unansweredCount)
        .arg(session.timedOut ? QStringLiteral(" · 时间到自动交卷") : QString()));
    resultList_->clear();
    for (qsizetype index = 0; index < session.resultItems.size(); ++index) {
        const domain::ExamResultItem &item = session.resultItems.at(index);
        const QString state = item.correct
            ? QStringLiteral("正确")
            : (item.unanswered ? QStringLiteral("未答") : QStringLiteral("错误"));
        const QString explanation = item.questionSnapshot.builtinExplanation.text.trimmed();
        auto *row = new QListWidgetItem(
            QStringLiteral("%1. [%2] %3\n你的答案：%4 · 正确答案：%5%6")
                .arg(index + 1)
                .arg(state)
                .arg(item.questionSnapshot.prompt)
                .arg(item.answer.isEmpty() ? QStringLiteral("未答") : item.answer)
                .arg(item.questionSnapshot.correctAnswer)
                .arg(explanation.isEmpty()
                    ? QString() : QStringLiteral("\n解析：%1").arg(explanation)),
            resultList_);
        row->setData(Qt::UserRole, item.questionId);
        row->setSizeHint(QSize(0, explanation.isEmpty() ? 78 : 106));
    }
    Q_UNUSED(fromHistory)
}

std::optional<domain::ExamSession> ExamPage::loadActive(QString *error) const
{
    if (databasePath_.isEmpty()) {
        return std::nullopt;
    }
    storage::Database database(connectionName(QStringLiteral("exam-active")));
    if (!database.open(databasePath_, error) || !database.migrate(error)) {
        return std::nullopt;
    }
    storage::SqliteExamRepository repository(database.connection());
    return repository.latestActive(error);
}

QVector<domain::ExamSession> ExamPage::loadHistory(QString *error) const
{
    if (databasePath_.isEmpty()) {
        return {};
    }
    storage::Database database(connectionName(QStringLiteral("exam-history")));
    if (!database.open(databasePath_, error) || !database.migrate(error)) {
        return {};
    }
    storage::SqliteExamRepository repository(database.connection());
    return repository.history(30, error);
}

QHash<QUuid, domain::Question> ExamPage::currentQuestionMap() const
{
    QHash<QUuid, domain::Question> result;
    if (!activeSession_) {
        return result;
    }
    for (const QUuid &id : std::as_const(activeSession_->questionOrder)) {
        if (questionsById_.contains(id)) {
            result.insert(id, questionsById_.value(id));
        }
    }
    return result;
}

QString ExamPage::formattedTime(int seconds) const
{
    seconds = std::max(0, seconds);
    return QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

void ExamPage::setStatus(const QString &message, bool error)
{
    setupStatus_->setText(message);
    setupStatus_->setProperty("error", error);
    setupStatus_->style()->unpolish(setupStatus_);
    setupStatus_->style()->polish(setupStatus_);
}

bool ExamPage::hasActiveExam() const
{
    return activeSession_.has_value();
}

void ExamPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyResponsiveHeader();
}

void ExamPage::applyResponsiveHeader()
{
    if (!examHeaderLayout_ || !examBackButton_ || !submitButton_) {
        return;
    }
    while (examHeaderLayout_->count() > 0) {
        delete examHeaderLayout_->takeAt(0);
    }
    if (width() < 720) {
        examHeaderLayout_->addWidget(examBackButton_, 0, 0);
        examHeaderLayout_->addWidget(examTitle_, 0, 1, 1, 2);
        examHeaderLayout_->addWidget(pauseButton_, 1, 0);
        examHeaderLayout_->addWidget(timerLabel_, 1, 1);
        examHeaderLayout_->addWidget(submitButton_, 1, 2);
        examHeaderLayout_->setColumnStretch(1, 1);
    } else {
        examHeaderLayout_->addWidget(examBackButton_, 0, 0);
        examHeaderLayout_->addWidget(examTitle_, 0, 1);
        examHeaderLayout_->addWidget(pauseButton_, 0, 2);
        examHeaderLayout_->addWidget(timerLabel_, 0, 3);
        examHeaderLayout_->addWidget(submitButton_, 0, 4);
        examHeaderLayout_->setColumnStretch(1, 1);
    }
}

void ExamPage::handleBack()
{
    if (pages_->currentWidget() == examPage_) {
        requestExitExam();
    } else if (pages_->currentWidget() == resultPage_) {
        showSetup();
    } else {
        emit backRequested();
    }
}

} // namespace quizapp::ui
