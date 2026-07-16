#pragma once

#include "domain/PracticeSession.h"
#include "domain/Question.h"
#include "domain/QuestionBank.h"

#include <QWidget>

class QLabel;
class QGridLayout;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QSortFilterProxyModel;
class QTableView;

namespace quizapp::ui {

class AnswerTableModel;

class AnswerTablePage final : public QWidget {
    Q_OBJECT

public:
    explicit AnswerTablePage(QWidget *parent = nullptr);

    void start(
        const domain::InstalledBankSummary &scope,
        const QVector<domain::Question> &questions,
        const domain::PracticeSession &session);
    qsizetype currentQuestionIndex() const;
    void setCurrentQuestionIndex(qsizetype questionIndex);
    bool hasContent() const;

signals:
    void backRequested();
    void currentQuestionChanged(qsizetype questionIndex);
    void detailRequested(qsizetype questionIndex);
    void handwritingRequested(qsizetype questionIndex);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateResponsiveColumns();
    void updateSelectionActions();
    void emitForCurrentSelection(bool handwriting);

    AnswerTableModel *model_ = nullptr;
    QSortFilterProxyModel *proxyModel_ = nullptr;
    QTableView *table_ = nullptr;
    QLineEdit *search_ = nullptr;
    QGridLayout *headerLayout_ = nullptr;
    QPushButton *backButton_ = nullptr;
    QWidget *titleContainer_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *summary_ = nullptr;
    QPushButton *detailButton_ = nullptr;
    QPushButton *handwritingButton_ = nullptr;
};

} // namespace quizapp::ui
