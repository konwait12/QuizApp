#include "ui/QuestionOverviewDialog.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>

namespace quizapp::ui {

QuestionOverviewDialog::QuestionOverviewDialog(
    const QVector<domain::Question> &questions,
    const domain::PracticeSession &session,
    QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("questionOverviewDialog"));
    setWindowTitle(QStringLiteral("题号总览"));
    setModal(true);
    setAttribute(Qt::WA_DeleteOnClose);
    const bool compact = parent && parent->width() < 700;

    QHash<QUuid, const domain::Question *> byId;
    for (const domain::Question &question : questions) {
        byId.insert(question.id, &question);
    }

    int answeredCount = 0;
    int correctCount = 0;
    int wrongCount = 0;
    for (const QUuid &questionId : session.questionOrder) {
        const auto found = byId.constFind(questionId);
        if (found == byId.cend()) {
            continue;
        }
        const QString status = statusFor(**found, session);
        if (status != QStringLiteral("unanswered")) {
            ++answeredCount;
        }
        if (status == QStringLiteral("correct")) {
            ++correctCount;
        } else if (status == QStringLiteral("wrong")) {
            ++wrongCount;
        }
    }

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 18);
    root->setSpacing(14);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("题号总览"), this);
    title->setObjectName(QStringLiteral("questionOverviewTitle"));
    auto *closeButton = new QToolButton(this);
    closeButton->setObjectName(QStringLiteral("questionOverviewCloseButton"));
    closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    closeButton->setIconSize(QSize(20, 20));
    closeButton->setToolTip(QStringLiteral("关闭"));
    closeButton->setAccessibleName(QStringLiteral("关闭题号总览"));
    closeButton->setFixedSize(40, 40);
    connect(closeButton, &QToolButton::clicked, this, &QDialog::reject);
    header->addWidget(title);
    header->addStretch();
    header->addWidget(closeButton);
    root->addLayout(header);

    auto *summaryBar = new QFrame(this);
    summaryBar->setObjectName(QStringLiteral("questionOverviewSummaryBar"));
    auto *summaryLayout = new QHBoxLayout(summaryBar);
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    summaryLayout->setSpacing(8);
    const int totalCount = session.questionOrder.size();
    const std::array<std::pair<QString, QString>, 4> statistics{{
        {QStringLiteral("已答 %1/%2").arg(answeredCount).arg(totalCount),
         QStringLiteral("answered")},
        {QStringLiteral("正确 %1").arg(correctCount), QStringLiteral("correct")},
        {QStringLiteral("错误 %1").arg(wrongCount), QStringLiteral("wrong")},
        {QStringLiteral("未答 %1").arg(qMax(0, totalCount - answeredCount)),
         QStringLiteral("unanswered")},
    }};
    for (const auto &[text, status] : statistics) {
        auto *stat = new QLabel(text, summaryBar);
        stat->setObjectName(QStringLiteral("questionOverviewStat"));
        stat->setProperty("answerStatus", status);
        stat->setAlignment(Qt::AlignCenter);
        stat->setAccessibleName(text);
        summaryLayout->addWidget(stat);
    }
    root->addWidget(summaryBar);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("questionOverviewScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(14);

    struct Group {
        QString title;
        QVector<qsizetype> positions;
    };
    QVector<Group> groups;
    if (session.mode == domain::PracticeMode::Random) {
        Group group{QStringLiteral("随机顺序"), {}};
        for (qsizetype index = 0; index < session.questionOrder.size(); ++index) {
            group.positions.append(index);
        }
        groups.append(group);
    } else {
        const std::array<domain::QuestionType, 4> types{{
            domain::QuestionType::Single,
            domain::QuestionType::Multiple,
            domain::QuestionType::Boolean,
            domain::QuestionType::Subjective,
        }};
        for (domain::QuestionType type : types) {
            Group group{questionTypeText(type), {}};
            for (qsizetype index = 0; index < session.questionOrder.size(); ++index) {
                const auto found = byId.constFind(session.questionOrder.at(index));
                if (found != byId.cend() && (*found)->type == type) {
                    group.positions.append(index);
                }
            }
            if (!group.positions.isEmpty()) {
                groups.append(group);
            }
        }
    }

    const int columns = compact ? 5 : 10;
    int totalRows = 0;
    for (const Group &group : groups) {
        totalRows += static_cast<int>((group.positions.size() + columns - 1) / columns);
    }
    const int desiredHeight = 190 + totalRows * 52 + groups.size() * 34;
    const int availableHeight = parent ? qMax(360, parent->height() - 40) : 680;
    const int dialogWidth = compact && parent
        ? qMax(340, parent->width() - 20)
        : qMin(720, parent ? parent->width() - 80 : 720);
    resize(dialogWidth, qMin(availableHeight, qMax(360, desiredHeight)));
    for (const Group &group : groups) {
        auto *sectionLabel = new QLabel(
            QStringLiteral("%1 · %2 题").arg(group.title).arg(group.positions.size()),
            content);
        sectionLabel->setObjectName(QStringLiteral("questionOverviewSection"));
        contentLayout->addWidget(sectionLabel);
        auto *grid = new QGridLayout;
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(8);
        for (qsizetype itemIndex = 0; itemIndex < group.positions.size(); ++itemIndex) {
            const qsizetype position = group.positions.at(itemIndex);
            const QUuid questionId = session.questionOrder.at(position);
            const auto found = byId.constFind(questionId);
            if (found == byId.cend()) {
                continue;
            }
            auto *button = new QPushButton(QString::number(position + 1), content);
            button->setObjectName(QStringLiteral("questionOverviewNumberButton"));
            button->setFixedSize(44, 44);
            button->setProperty("answerStatus", statusFor(**found, session));
            button->setProperty("currentQuestion", position == session.currentIndex);
            button->setAccessibleName(QStringLiteral("第%1题，%2")
                .arg(position + 1)
                .arg(group.title));
            connect(button, &QPushButton::clicked, this, [this, position] {
                emit questionSelected(position);
                accept();
            });
            grid->addWidget(button, itemIndex / columns, itemIndex % columns);
        }
        grid->setColumnStretch(columns, 1);
        contentLayout->addLayout(grid);
    }
    contentLayout->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);
}

QString QuestionOverviewDialog::statusFor(
    const domain::Question &question,
    const domain::PracticeSession &session) const
{
    QString answer = session.drafts.value(question.id);
    if (answer.isEmpty()) {
        answer = session.answers.value(question.id);
    }
    if (answer.isEmpty()) {
        return QStringLiteral("unanswered");
    }
    const QString correctAnswer = question.correctAnswer.trimmed().toUpper();
    if (correctAnswer.isEmpty() || question.type == domain::QuestionType::Subjective) {
        return QStringLiteral("answered");
    }
    return answer.trimmed().toUpper() == correctAnswer
        ? QStringLiteral("correct") : QStringLiteral("wrong");
}

QString QuestionOverviewDialog::questionTypeText(domain::QuestionType type) const
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

} // namespace quizapp::ui
