#include "ui/StudyHubPage.h"
#include "ui/StudyTrendChart.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>

namespace quizapp::ui {
namespace {

QFrame *statCard(const QString &caption, QWidget *parent, QLabel **valueOutput)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("studySummaryCard"));
    card->setMinimumSize(128, 82);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(15, 13, 15, 13);
    layout->setSpacing(3);
    auto *value = new QLabel(QStringLiteral("0"), card);
    value->setObjectName(QStringLiteral("studySummaryValue"));
    auto *label = new QLabel(caption, card);
    label->setObjectName(QStringLiteral("secondaryText"));
    layout->addWidget(value);
    layout->addWidget(label);
    *valueOutput = value;
    return card;
}

QString questionLabel(const domain::Question &question)
{
    QString prompt = question.prompt.simplified();
    if (prompt.size() > 80) {
        prompt = prompt.left(80) + QStringLiteral("...");
    }
    const QString path = question.path.join(QStringLiteral(" / "));
    return path.isEmpty() ? prompt : QStringLiteral("%1\n%2").arg(path, prompt);
}

} // namespace

StudyHubPage::StudyHubPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("studyHubPage"));
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("studyHubScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *container = new QWidget(scroll);
    auto *containerLayout = new QHBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->addStretch();
    auto *content = new QWidget(container);
    content->setMaximumWidth(1040);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 24, 24, 32);
    layout->setSpacing(16);

    auto *title = new QLabel(QStringLiteral("学习中心"), content);
    title->setObjectName(QStringLiteral("pageHeading"));
    layout->addWidget(title);
    auto *subtitle = new QLabel(
        QStringLiteral("按到期顺序复习，评分后自动安排下次时间"), content);
    subtitle->setObjectName(QStringLiteral("pageSupportingText"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    summaryGrid_ = new QGridLayout;
    summaryGrid_->setContentsMargins(0, 0, 0, 0);
    summaryGrid_->setSpacing(10);
    summaryValues_.resize(4);
    summaryCards_ = {
        statCard(QStringLiteral("待复习"), content, &summaryValues_[0]),
        statCard(QStringLiteral("复习卡"), content, &summaryValues_[1]),
        statCard(QStringLiteral("新卡"), content, &summaryValues_[2]),
        statCard(QStringLiteral("今日学习"), content, &summaryValues_[3]),
    };
    layout->addLayout(summaryGrid_);

    auto *header = new QHBoxLayout;
    auto *sectionTitle = new QLabel(QStringLiteral("今日队列"), content);
    sectionTitle->setObjectName(QStringLiteral("sectionHeading"));
    auto *refresh = new QPushButton(QStringLiteral("刷新"), content);
    refresh->setObjectName(QStringLiteral("studyRefreshButton"));
    refresh->setProperty("quizappIcon", QStringLiteral("school"));
    refresh->setMinimumHeight(40);
    connect(refresh, &QPushButton::clicked, this, &StudyHubPage::refreshRequested);
    header->addWidget(sectionTitle);
    header->addStretch();
    header->addWidget(refresh);
    layout->addLayout(header);

    queueStatus_ = new QLabel(content);
    queueStatus_->setObjectName(QStringLiteral("pageSupportingText"));
    queueStatus_->setWordWrap(true);
    layout->addWidget(queueStatus_);

    dueList_ = new QListWidget(content);
    dueList_->setObjectName(QStringLiteral("studyDueList"));
    dueList_->setAlternatingRowColors(false);
    dueList_->setSelectionMode(QAbstractItemView::NoSelection);
    dueList_->setMinimumHeight(180);
    layout->addWidget(dueList_);

    startReviewButton_ = new QPushButton(QStringLiteral("开始复习"), content);
    startReviewButton_->setObjectName(QStringLiteral("studyStartReviewButton"));
    startReviewButton_->setProperty("quizappIcon", QStringLiteral("school"));
    startReviewButton_->setMinimumHeight(48);
    connect(startReviewButton_, &QPushButton::clicked,
            this, &StudyHubPage::startReviewRequested);
    layout->addWidget(startReviewButton_);

    auto *trendHeader = new QHBoxLayout;
    auto *trendTitle = new QLabel(QStringLiteral("学习趋势"), content);
    trendTitle->setObjectName(QStringLiteral("sectionHeading"));
    studyRangeChoice_ = new QComboBox(content);
    studyRangeChoice_->setObjectName(QStringLiteral("studyRangeChoice"));
    studyRangeChoice_->addItem(QStringLiteral("近 7 天"), 7);
    studyRangeChoice_->addItem(QStringLiteral("近 30 天"), 30);
    studyRangeChoice_->addItem(QStringLiteral("近 90 天"), 90);
    studyRangeChoice_->setMinimumHeight(40);
    connect(studyRangeChoice_, &QComboBox::currentIndexChanged, this, [this] {
        emit studyRangeChanged(studyRangeChoice_->currentData().toInt());
    });
    trendHeader->addWidget(trendTitle);
    trendHeader->addStretch();
    trendHeader->addWidget(studyRangeChoice_);
    layout->addLayout(trendHeader);
    studyTrendChart_ = new StudyTrendChart(content);
    layout->addWidget(studyTrendChart_);
    layout->addStretch();

    containerLayout->addWidget(content, 1);
    containerLayout->addStretch();
    scroll->setWidget(container);
    root->addWidget(scroll);
    applyResponsiveLayout();
    setReviewData({}, {});
}

void StudyHubPage::setReviewData(
    const domain::ReviewStats &stats,
    const QVector<domain::Question> &dueQuestions)
{
    summaryValues_[0]->setText(QString::number(stats.due));
    summaryValues_[1]->setText(QString::number(stats.total));
    summaryValues_[2]->setText(QString::number(stats.fresh));
    dueList_->clear();
    for (const domain::Question &question : dueQuestions) {
        auto *item = new QListWidgetItem(questionLabel(question), dueList_);
        item->setData(Qt::UserRole, question.id);
        item->setToolTip(question.prompt);
        item->setSizeHint(QSize(0, 58));
    }
    const bool hasDue = !dueQuestions.isEmpty();
    queueStatus_->setText(hasDue
        ? QStringLiteral("当前有 %1 道题到期，按最早到期顺序排列。")
              .arg(dueQuestions.size())
        : (stats.total > 0
              ? QStringLiteral("今天的复习已完成。")
              : QStringLiteral("在刷题页将需要巩固的题目加入复习。")));
    dueList_->setVisible(hasDue);
    startReviewButton_->setEnabled(hasDue);
    startReviewButton_->setText(hasDue
        ? QStringLiteral("开始复习 · %1").arg(dueQuestions.size())
        : QStringLiteral("暂无到期题目"));
}

void StudyHubPage::setTodayStudySeconds(int seconds)
{
    const int bounded = qMax(0, seconds);
    const int hours = bounded / 3600;
    const int minutes = (bounded % 3600) / 60;
    summaryValues_[3]->setText(hours > 0
        ? QStringLiteral("%1时%2分").arg(hours).arg(minutes)
        : QStringLiteral("%1 分钟").arg(minutes));
}

void StudyHubPage::setStudyTrend(
    const QVector<domain::DailyStudyTotal> &totals,
    bool animate)
{
    studyTrendChart_->setData(totals, animate);
}

void StudyHubPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyResponsiveLayout();
}

void StudyHubPage::applyResponsiveLayout()
{
    if (!summaryGrid_) {
        return;
    }
    while (summaryGrid_->count() > 0) {
        summaryGrid_->takeAt(0);
    }
    const int columns = width() >= 760 ? 4 : 2;
    for (qsizetype index = 0; index < summaryCards_.size(); ++index) {
        summaryGrid_->addWidget(
            summaryCards_.at(index),
            static_cast<int>(index) / columns,
            static_cast<int>(index) % columns);
    }
    for (int column = 0; column < columns; ++column) {
        summaryGrid_->setColumnStretch(column, 1);
    }
}

} // namespace quizapp::ui
