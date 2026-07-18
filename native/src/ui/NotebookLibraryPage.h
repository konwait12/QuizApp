#pragma once

#include "domain/Notebook.h"
#include "services/NotebookService.h"

#include <QWidget>

#include <memory>

class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QToolButton;

namespace quizapp::ui {

class NotebookDatabaseHolder;

class NotebookLibraryPage final : public QWidget {
    Q_OBJECT

public:
    NotebookLibraryPage(QString databasePath, QString dataRoot, QWidget *parent = nullptr);
    ~NotebookLibraryPage() override;

    void refresh();
    void markSaved(const QUuid &notebookId);
    std::optional<domain::NotebookRecord> record(const QUuid &notebookId) const;

signals:
    void backRequested();
    void openRequested(const domain::NotebookRecord &record);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void createNotebook();
    void openSelected();
    void renameSelected();
    void recycleOrRestoreSelected();
    void permanentlyDeleteSelected();
    void setRecycleView(bool enabled);
    void updateActions();
    void applyResponsiveLayout();
    std::optional<domain::NotebookRecord> selectedRecord() const;
    void setStatus(const QString &status, bool error = false);

    std::unique_ptr<NotebookDatabaseHolder> databaseHolder_;
    std::unique_ptr<services::NotebookService> service_;
    QVector<domain::NotebookRecord> records_;
    bool recycleView_ = false;
    QLineEdit *titleInput_ = nullptr;
    QPushButton *createButton_ = nullptr;
    QToolButton *recycleViewButton_ = nullptr;
    QListWidget *list_ = nullptr;
    QLabel *emptyLabel_ = nullptr;
    QLabel *status_ = nullptr;
    QGridLayout *actionsLayout_ = nullptr;
    QPushButton *openButton_ = nullptr;
    QPushButton *renameButton_ = nullptr;
    QPushButton *recycleButton_ = nullptr;
    QPushButton *permanentDeleteButton_ = nullptr;
};

} // namespace quizapp::ui
