#pragma once

#include "domain/Question.h"
#include "domain/ReviewCard.h"
#include "domain/StudyEvent.h"

#include <QVector>
#include <QWidget>

class QGridLayout;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QResizeEvent;

namespace quizapp::ui {

class StudyTrendChart;

class StudyHubPage final : public QWidget {
    Q_OBJECT

public:
    explicit StudyHubPage(QWidget *parent = nullptr);

    void setReviewData(
        const domain::ReviewStats &stats,
        const QVector<domain::Question> &dueQuestions);
    void setTodayStudySeconds(int seconds);
    void setStudyTrend(const QVector<domain::DailyStudyTotal> &totals, bool animate);

signals:
    void startReviewRequested();
    void refreshRequested();
    void studyRangeChanged(int days);
    void openExamRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyResponsiveLayout();

    QGridLayout *summaryGrid_ = nullptr;
    QVector<QWidget *> summaryCards_;
    QVector<QLabel *> summaryValues_;
    QLabel *queueStatus_ = nullptr;
    QListWidget *dueList_ = nullptr;
    QPushButton *startReviewButton_ = nullptr;
    QComboBox *studyRangeChoice_ = nullptr;
    StudyTrendChart *studyTrendChart_ = nullptr;
};

} // namespace quizapp::ui
