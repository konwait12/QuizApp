#pragma once

#include "domain/PracticeSession.h"
#include "domain/Question.h"
#include "domain/ReviewCard.h"

#include <array>
#include <QWidget>

class QLabel;
class QFrame;
class QGridLayout;
class QPushButton;
class QResizeEvent;
class QVBoxLayout;

namespace quizapp::ui {

class ReviewPage final : public QWidget {
    Q_OBJECT

public:
    explicit ReviewPage(QString dataRoot = QString(), QWidget *parent = nullptr);

    void showCard(
        const domain::Question &question,
        const domain::ReviewCard &card,
        const std::array<domain::ReviewPreview, 4> &previews,
        int remainingCount,
        const QUuid &sessionId);
    void showEmpty();
    std::optional<QUuid> currentQuestionId() const;
    domain::NotebookLaunchContext notebookContext() const;

signals:
    void backRequested();
    void ratingRequested(const QUuid &questionId, domain::ReviewRating rating);
    void removeRequested(const QUuid &questionId);
    void handwritingRequested(const domain::NotebookLaunchContext &context);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void render();
    void updateResponsiveHeader();
    void clearLayout(QVBoxLayout *layout);
    void addImages(
        QVBoxLayout *layout,
        const QStringList &blobIds,
        const QString &objectName);
    QString intervalText(int days) const;

    QString dataRoot_;
    domain::Question question_;
    domain::ReviewCard card_;
    std::array<domain::ReviewPreview, 4> previews_{};
    int remainingCount_ = 0;
    QUuid sessionId_;
    bool revealed_ = false;
    QUuid renderedQuestionId_;

    QLabel *progressLabel_ = nullptr;
    QGridLayout *topLayout_ = nullptr;
    QPushButton *backButton_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *pathLabel_ = nullptr;
    QLabel *promptLabel_ = nullptr;
    QVBoxLayout *questionImagesLayout_ = nullptr;
    QVBoxLayout *optionsLayout_ = nullptr;
    QFrame *answerSurface_ = nullptr;
    QLabel *answerLabel_ = nullptr;
    QLabel *explanationLabel_ = nullptr;
    QVBoxLayout *explanationImagesLayout_ = nullptr;
    QVBoxLayout *actionsLayout_ = nullptr;
    QPushButton *revealButton_ = nullptr;
    std::array<QPushButton *, 4> ratingButtons_{};
    QPushButton *removeButton_ = nullptr;
    QPushButton *handwritingButton_ = nullptr;
};

} // namespace quizapp::ui
