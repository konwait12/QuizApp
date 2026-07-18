#pragma once

#include "domain/PracticeSession.h"
#include "domain/Notebook.h"

#include <QString>
#include <QVector>
#include <QWidget>

#include <memory>

class Document;
class DocumentViewport;
class QFrame;
class QHBoxLayout;
class QLabel;
class QKeyEvent;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace quizapp::ui {

class HandwritingPage final : public QWidget {
    Q_OBJECT

public:
    explicit HandwritingPage(QString dataRoot, QWidget *parent = nullptr);
    ~HandwritingPage() override;

    void openNotebook(const domain::NotebookLaunchContext &context);
    bool openFreeNotebook(const domain::NotebookRecord &record);
    bool hasNotebookOpen() const;
    domain::NotebookLaunchContext currentContext() const;
    QString currentBundlePath() const;
    DocumentViewport *viewport() const;

public slots:
    void saveAndReturn();

signals:
    void returnToPractice(const domain::NotebookLaunchContext &context);
    void returnToNotebookLibrary(const QUuid &notebookId);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QString effectiveDataRoot() const;
    QString questionKey() const;
    QString questionNotesDirectory() const;
    QString bundlePathForCurrentQuestion() const;
    QString viewportStatePathForCurrentQuestion() const;
    bool setFreeNotebookPath(const QString &relativePath);
    void restoreViewportState();
    bool saveDocument(QString *errorMessage = nullptr);
    bool saveViewportState(QString *errorMessage = nullptr) const;
    void setStatus(const QString &text);
    void applyResponsiveLayout();
    void refreshPageControls();
    void updatePageState(int pageIndex);
    void updateZoomLabel(qreal zoom);
    void addPage();
    void goToRelativePage(int delta);

    QString dataRoot_;
    domain::NotebookLaunchContext context_;
    std::unique_ptr<Document> document_;
    QWidget *toolbar_ = nullptr;
    QVBoxLayout *toolbarLayout_ = nullptr;
    QWidget *toolStrip_ = nullptr;
    QScrollArea *toolScroller_ = nullptr;
    QFrame *pagePanel_ = nullptr;
    QVBoxLayout *pageButtonsLayout_ = nullptr;
    QWidget *mobilePageBar_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *desktopPageLabel_ = nullptr;
    QLabel *mobilePageLabel_ = nullptr;
    QLabel *zoomLabel_ = nullptr;
    QToolButton *previousPageButton_ = nullptr;
    QToolButton *nextPageButton_ = nullptr;
    QPushButton *addPageButton_ = nullptr;
    QVector<QPushButton *> pageButtons_;
    DocumentViewport *viewport_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QString currentBundlePath_;
    QUuid freeNotebookId_;
    QString freeNotebookTitle_;
    bool freeNotebookOpen_ = false;
    bool returning_ = false;
};

} // namespace quizapp::ui
